/**
 * @file dap_mlkem_poly_simd.h
 * @brief ML-KEM SIMD — declarations, static bodies, dispatch. All generated.
 * @details Generated from dap_mlkem_poly_simd.h.tpl by dap_tpl.
 *
 * Two dispatch levels:
 * - Fast ops (csubq, reduce, tomont, add, sub): static bodies + inline dispatch.
 * - Heavy ops (compress, basemul, NTT): extern declarations for pointer dispatch.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#pragma once
#include <stdint.h>
#include "dap_cpu_arch.h"
#include "dap_mlkem_reduce.h"

/* AArch64 hosts, or ARMv7 with NEON (Android armeabi-v7a). */
#if defined(__aarch64__) || defined(_M_ARM64) || (defined(__ARM_NEON) && defined(__arm__))
#define DAP_MLKEM_HAVE_NEON 1
#endif

/* ============================================================================
 *  x86_64 — AVX2 section
 * ============================================================================ */

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>

/* --- Extern heavy-function declarations --- */
void dap_mlkem_poly_basemul_montgomery_avx2(int16_t *, const int16_t *, const int16_t *, const int16_t *);
void dap_mlkem_poly_basemul_acc_montgomery_avx2(int16_t *, const int16_t * const *, const int16_t * const *, unsigned);
void dap_mlkem_poly_compress_coeffs_avx2(int16_t *, int16_t, int16_t);
void dap_mlkem_poly_compress_d4_avx2(uint8_t *, const int16_t *);
void dap_mlkem_poly_compress_d5_avx2(uint8_t *, const int16_t *);
void dap_mlkem_poly_decompress_d4_avx2(int16_t *, const uint8_t *);
void dap_mlkem_poly_decompress_d5_avx2(int16_t *, const uint8_t *);
void dap_mlkem_poly_tobytes_avx2(uint8_t *, const int16_t *);
void dap_mlkem_poly_frombytes_avx2(int16_t *, const uint8_t *);
void dap_mlkem_poly_frommsg_avx2(int16_t *, const uint8_t *);
void dap_mlkem_poly_tomsg_avx2(uint8_t *, const int16_t *);
void dap_mlkem_poly_mulcache_compute_avx2(int16_t *, const int16_t *);
void dap_mlkem_polyvec_compress_d10_avx2(uint8_t *, const int16_t *);
void dap_mlkem_polyvec_compress_d11_avx2(uint8_t *, const int16_t *);
void dap_mlkem_polyvec_decompress_d10_avx2(int16_t *, const uint8_t *);
void dap_mlkem_polyvec_decompress_d11_avx2(int16_t *, const uint8_t *);
void dap_mlkem_polyvec_basemul_acc_cached_avx2(int16_t *, const int16_t * const *, const int16_t * const *, const int16_t * const *, unsigned);

/* --- Static fast-function bodies (inlinable at call site) --- */
#ifdef __GNUC__
#pragma GCC push_options
#pragma GCC target("avx2")
#endif

/* AVX2 static fast-function bodies for ML-KEM poly ops.
 * Included by generated dap_mlkem_poly_simd.h inside #pragma GCC target("avx2") scope. */

__attribute__((target("avx2")))
static void s_mlkem_poly_csubq_avx2(int16_t *a)
{
    const __m256i q = _mm256_set1_epi16(3329);
    for (unsigned i = 0; i < 256; i += 16) {
        __m256i v = _mm256_loadu_si256((const __m256i *)(a + i));
        v = _mm256_sub_epi16(v, q);
        __m256i mask = _mm256_srai_epi16(v, 15);
        v = _mm256_add_epi16(v, _mm256_and_si256(mask, q));
        _mm256_storeu_si256((__m256i *)(a + i), v);
    }
}

__attribute__((target("avx2")))
static void s_mlkem_poly_reduce_avx2(int16_t *a)
{
    const __m256i bv = _mm256_set1_epi16(20159);
    const __m256i q  = _mm256_set1_epi16(3329);
    for (unsigned i = 0; i < 256; i += 16) {
        __m256i v = _mm256_loadu_si256((const __m256i *)(a + i));
        __m256i t = _mm256_mulhi_epi16(v, bv);
        t = _mm256_srai_epi16(t, 10);
        v = _mm256_sub_epi16(v, _mm256_mullo_epi16(t, q));
        __m256i sign = _mm256_srai_epi16(v, 15);
        v = _mm256_add_epi16(v, _mm256_and_si256(sign, q));
        _mm256_storeu_si256((__m256i *)(a + i), v);
    }
}

__attribute__((target("avx2")))
static void s_mlkem_poly_tomont_avx2(int16_t *a)
{
    const __m256i f    = _mm256_set1_epi16((int16_t)((1ULL << 32) % 3329));
    const __m256i qinv = _mm256_set1_epi16((int16_t)-3327);
    const __m256i q    = _mm256_set1_epi16(3329);
    for (unsigned i = 0; i < 256; i += 16) {
        __m256i v  = _mm256_loadu_si256((const __m256i *)(a + i));
        __m256i lo = _mm256_mullo_epi16(v, f);
        __m256i hi = _mm256_mulhi_epi16(v, f);
        __m256i u  = _mm256_mullo_epi16(lo, qinv);
        __m256i uq = _mm256_mulhi_epi16(u, q);
        _mm256_storeu_si256((__m256i *)(a + i), _mm256_sub_epi16(hi, uq));
    }
}

__attribute__((target("avx2")))
static void s_mlkem_poly_add_avx2(int16_t *r, const int16_t *a, const int16_t *b)
{
    for (unsigned i = 0; i < 256; i += 16) {
        __m256i va = _mm256_loadu_si256((const __m256i *)(a + i));
        __m256i vb = _mm256_loadu_si256((const __m256i *)(b + i));
        _mm256_storeu_si256((__m256i *)(r + i), _mm256_add_epi16(va, vb));
    }
}

__attribute__((target("avx2")))
static void s_mlkem_poly_sub_avx2(int16_t *r, const int16_t *a, const int16_t *b)
{
    for (unsigned i = 0; i < 256; i += 16) {
        __m256i va = _mm256_loadu_si256((const __m256i *)(a + i));
        __m256i vb = _mm256_loadu_si256((const __m256i *)(b + i));
        _mm256_storeu_si256((__m256i *)(r + i), _mm256_sub_epi16(va, vb));
    }
}

__attribute__((target("avx2")))
static void s_mlkem_cbd2_avx2(int16_t *r, const uint8_t *buf)
{
    const __m256i mask55 = _mm256_set1_epi32(0x55555555);
    const __m256i mask33 = _mm256_set1_epi32(0x33333333);
    const __m256i mask03 = _mm256_set1_epi32(0x03030303);
    const __m256i mask0F = _mm256_set1_epi32(0x0F0F0F0F);

    for (unsigned i = 0; i < 256 / 64; i++) {
        __m256i f0 = _mm256_loadu_si256((const __m256i *)(buf + 32 * i));

        __m256i f1 = _mm256_srli_epi16(f0, 1);
        f0 = _mm256_and_si256(mask55, f0);
        f1 = _mm256_and_si256(mask55, f1);
        f0 = _mm256_add_epi8(f0, f1);

        f1 = _mm256_srli_epi16(f0, 2);
        f0 = _mm256_and_si256(mask33, f0);
        f1 = _mm256_and_si256(mask33, f1);
        f0 = _mm256_add_epi8(f0, mask33);
        f0 = _mm256_sub_epi8(f0, f1);

        f1 = _mm256_srli_epi16(f0, 4);
        f0 = _mm256_and_si256(mask0F, f0);
        f1 = _mm256_and_si256(mask0F, f1);
        f0 = _mm256_sub_epi8(f0, mask03);
        f1 = _mm256_sub_epi8(f1, mask03);

        __m256i f2 = _mm256_unpacklo_epi8(f0, f1);
        __m256i f3 = _mm256_unpackhi_epi8(f0, f1);

        f0 = _mm256_cvtepi8_epi16(_mm256_castsi256_si128(f2));
        f1 = _mm256_cvtepi8_epi16(_mm256_extracti128_si256(f2, 1));
        f2 = _mm256_cvtepi8_epi16(_mm256_castsi256_si128(f3));
        f3 = _mm256_cvtepi8_epi16(_mm256_extracti128_si256(f3, 1));

        _mm256_storeu_si256((__m256i *)(r + 64 * i +  0), f0);
        _mm256_storeu_si256((__m256i *)(r + 64 * i + 16), f2);
        _mm256_storeu_si256((__m256i *)(r + 64 * i + 32), f1);
        _mm256_storeu_si256((__m256i *)(r + 64 * i + 48), f3);
    }
}

__attribute__((target("avx2")))
static void s_mlkem_cbd3_avx2(int16_t *r, const uint8_t *buf)
{
    const __m256i mask249 = _mm256_set1_epi32(0x249249);
    const __m256i mask6DB = _mm256_set1_epi32(0x6DB6DB);
    const __m256i mask07  = _mm256_set1_epi32(7);
    const __m256i mask70  = _mm256_set1_epi32(7 << 16);
    const __m256i mask3   = _mm256_set1_epi16(3);
    const __m256i shufb   = _mm256_set_epi8(
        -1,15,14,13, -1,12,11,10, -1, 9, 8, 7, -1, 6, 5, 4,
        -1,11,10, 9, -1, 8, 7, 6, -1, 5, 4, 3, -1, 2, 1, 0);

    for (unsigned i = 0; i < 256 / 32; i++) {
        __m256i f0 = _mm256_loadu_si256((const __m256i *)(buf + 24 * i));
        f0 = _mm256_permute4x64_epi64(f0, 0x94);
        f0 = _mm256_shuffle_epi8(f0, shufb);

        __m256i f1 = _mm256_srli_epi32(f0, 1);
        __m256i f2 = _mm256_srli_epi32(f0, 2);
        f0 = _mm256_and_si256(mask249, f0);
        f1 = _mm256_and_si256(mask249, f1);
        f2 = _mm256_and_si256(mask249, f2);
        f0 = _mm256_add_epi32(f0, f1);
        f0 = _mm256_add_epi32(f0, f2);

        f1 = _mm256_srli_epi32(f0, 3);
        f0 = _mm256_add_epi32(f0, mask6DB);
        f0 = _mm256_sub_epi32(f0, f1);

        f1 = _mm256_slli_epi32(f0, 10);
        f2 = _mm256_srli_epi32(f0, 12);
        __m256i f3 = _mm256_srli_epi32(f0, 2);
        f0 = _mm256_and_si256(f0, mask07);
        f1 = _mm256_and_si256(f1, mask70);
        f2 = _mm256_and_si256(f2, mask07);
        f3 = _mm256_and_si256(f3, mask70);
        f0 = _mm256_add_epi16(f0, f1);
        f1 = _mm256_add_epi16(f2, f3);
        f0 = _mm256_sub_epi16(f0, mask3);
        f1 = _mm256_sub_epi16(f1, mask3);

        f2 = _mm256_unpacklo_epi32(f0, f1);
        f3 = _mm256_unpackhi_epi32(f0, f1);
        f0 = _mm256_permute2x128_si256(f2, f3, 0x20);
        f1 = _mm256_permute2x128_si256(f2, f3, 0x31);

        _mm256_storeu_si256((__m256i *)(r + 32 * i +  0), f0);
        _mm256_storeu_si256((__m256i *)(r + 32 * i + 16), f1);
    }
}

#ifdef __GNUC__
#pragma GCC pop_options
#endif

#endif /* x86_64 */

/* ============================================================================
 *  ARM NEON — AArch64 and ARMv7-a (when NEON is enabled)
 * ============================================================================ */

#if DAP_MLKEM_HAVE_NEON
#include <arm_neon.h>

/* --- Extern heavy-function declarations --- */
void dap_mlkem_poly_basemul_montgomery_neon(int16_t *, const int16_t *, const int16_t *, const int16_t *);
void dap_mlkem_poly_basemul_acc_montgomery_neon(int16_t *, const int16_t * const *, const int16_t * const *, unsigned);
void dap_mlkem_poly_compress_coeffs_neon(int16_t *, int16_t, int16_t);
void dap_mlkem_poly_compress_d4_neon(uint8_t *, const int16_t *);
void dap_mlkem_poly_compress_d5_neon(uint8_t *, const int16_t *);
void dap_mlkem_poly_decompress_d4_neon(int16_t *, const uint8_t *);
void dap_mlkem_poly_decompress_d5_neon(int16_t *, const uint8_t *);
void dap_mlkem_poly_tobytes_neon(uint8_t *, const int16_t *);
void dap_mlkem_poly_frombytes_neon(int16_t *, const uint8_t *);
void dap_mlkem_poly_frommsg_neon(int16_t *, const uint8_t *);
void dap_mlkem_poly_tomsg_neon(uint8_t *, const int16_t *);
void dap_mlkem_poly_mulcache_compute_neon(int16_t *, const int16_t *);
void dap_mlkem_polyvec_compress_d10_neon(uint8_t *, const int16_t *);
void dap_mlkem_polyvec_compress_d11_neon(uint8_t *, const int16_t *);
void dap_mlkem_polyvec_decompress_d10_neon(int16_t *, const uint8_t *);
void dap_mlkem_polyvec_decompress_d11_neon(int16_t *, const uint8_t *);
void dap_mlkem_polyvec_basemul_acc_cached_neon(int16_t *, const int16_t * const *, const int16_t * const *, const int16_t * const *, unsigned);

/* --- Static fast-function bodies (inlinable at call site) --- */
/* NEON static fast-function bodies for ML-KEM poly ops.
 * Included by generated dap_mlkem_poly_simd.h when DAP_MLKEM_HAVE_NEON. */

static void s_mlkem_poly_csubq_neon(int16_t *a)
{
    const int16x8_t q = vdupq_n_s16(3329);
    for (unsigned i = 0; i < 256; i += 8) {
        int16x8_t v = vld1q_s16(a + i);
        v = vsubq_s16(v, q);
        int16x8_t mask = vshrq_n_s16(v, 15);
        v = vaddq_s16(v, vandq_s16(mask, q));
        vst1q_s16(a + i, v);
    }
}

static void s_mlkem_poly_reduce_neon(int16_t *a)
{
    const int16x8_t bv = vdupq_n_s16(20159);
    const int16x8_t q  = vdupq_n_s16(3329);
    for (unsigned i = 0; i < 256; i += 8) {
        int16x8_t v = vld1q_s16(a + i);
        int16x4_t v_lo = vget_low_s16(v), v_hi = vget_high_s16(v);
        int16x4_t b_lo = vget_low_s16(bv), b_hi = vget_high_s16(bv);
        int16x8_t t = vcombine_s16(vshrn_n_s32(vmull_s16(v_lo, b_lo), 16),
                                    vshrn_n_s32(vmull_s16(v_hi, b_hi), 16));
        t = vshrq_n_s16(t, 10);
        v = vsubq_s16(v, vmulq_s16(t, q));
        int16x8_t sign = vshrq_n_s16(v, 15);
        v = vaddq_s16(v, vandq_s16(sign, q));
        vst1q_s16(a + i, v);
    }
}

static void s_mlkem_poly_tomont_neon(int16_t *a)
{
    const int16x8_t f    = vdupq_n_s16((int16_t)((1ULL << 32) % 3329));
    const int16x8_t qinv = vdupq_n_s16((int16_t)-3327);
    const int16x8_t q    = vdupq_n_s16(3329);
    for (unsigned i = 0; i < 256; i += 8) {
        int16x8_t v = vld1q_s16(a + i);
        int16x8_t lo = vmulq_s16(v, f);
        int16x4_t vl = vget_low_s16(v), vh = vget_high_s16(v);
        int16x4_t fl = vget_low_s16(f), fh = vget_high_s16(f);
        int16x8_t hi = vcombine_s16(vshrn_n_s32(vmull_s16(vl, fl), 16),
                                     vshrn_n_s32(vmull_s16(vh, fh), 16));
        int16x8_t u = vmulq_s16(lo, qinv);
        int16x4_t ul = vget_low_s16(u), uh = vget_high_s16(u);
        int16x4_t ql = vget_low_s16(q), qh = vget_high_s16(q);
        int16x8_t uq = vcombine_s16(vshrn_n_s32(vmull_s16(ul, ql), 16),
                                      vshrn_n_s32(vmull_s16(uh, qh), 16));
        vst1q_s16(a + i, vsubq_s16(hi, uq));
    }
}

static void s_mlkem_poly_add_neon(int16_t *r, const int16_t *a, const int16_t *b)
{
    for (unsigned i = 0; i < 256; i += 8) {
        int16x8_t va = vld1q_s16(a + i);
        int16x8_t vb = vld1q_s16(b + i);
        vst1q_s16(r + i, vaddq_s16(va, vb));
    }
}

static void s_mlkem_poly_sub_neon(int16_t *r, const int16_t *a, const int16_t *b)
{
    for (unsigned i = 0; i < 256; i += 8) {
        int16x8_t va = vld1q_s16(a + i);
        int16x8_t vb = vld1q_s16(b + i);
        vst1q_s16(r + i, vsubq_s16(va, vb));
    }
}

static void s_mlkem_cbd2_neon(int16_t *r, const uint8_t *buf)
{
    /* Bit-identical to s_mlkem_cbd2_ref — the prior vzip-based NEON output order did not match. */
    for (unsigned i = 0; i < 256 / 8; i++) {
        uint32_t t = (uint32_t)buf[4 * i] | ((uint32_t)buf[4 * i + 1] << 8)
                   | ((uint32_t)buf[4 * i + 2] << 16) | ((uint32_t)buf[4 * i + 3] << 24);
        uint32_t d = (t & 0x55555555) + ((t >> 1) & 0x55555555);
        for (unsigned j = 0; j < 8; j++) {
            int16_t a = (int16_t)((d >> (4 * j)) & 0x3);
            int16_t b = (int16_t)((d >> (4 * j + 2)) & 0x3);
            r[8 * i + j] = a - b;
        }
    }
}

static void s_mlkem_cbd3_neon(int16_t *r, const uint8_t *buf)
{
    for (unsigned i = 0; i < 256 / 4; i++) {
        uint32_t t = (uint32_t)buf[3*i] | ((uint32_t)buf[3*i+1] << 8) | ((uint32_t)buf[3*i+2] << 16);
        uint32_t d = (t & 0x00249249) + ((t >> 1) & 0x00249249) + ((t >> 2) & 0x00249249);
        for (unsigned j = 0; j < 4; j++) {
            int16_t a = (int16_t)((d >> (6 * j))     & 0x7);
            int16_t b = (int16_t)((d >> (6 * j + 3)) & 0x7);
            r[4 * i + j] = a - b;
        }
    }
}

#endif /* DAP_MLKEM_HAVE_NEON */

/* ============================================================================
 *  Scalar references — always available
 * ============================================================================ */

static inline void s_mlkem_poly_csubq_ref(int16_t *a)
{
    for (unsigned i = 0; i < 256; i++)
        a[i] = dap_mlkem_csubq(a[i]);
}

static inline void s_mlkem_poly_reduce_ref(int16_t *a)
{
    for (unsigned i = 0; i < 256; i++) {
        a[i] = dap_mlkem_barrett_reduce(a[i]);
        a[i] = dap_mlkem_caddq(a[i]);
    }
}

static inline void s_mlkem_poly_tomont_ref(int16_t *a)
{
    const int16_t f = (int16_t)((1ULL << 32) % 3329);
    for (unsigned i = 0; i < 256; i++)
        a[i] = dap_mlkem_montgomery_reduce((int32_t)a[i] * f);
}

static inline void s_mlkem_poly_add_ref(int16_t *r, const int16_t *a, const int16_t *b)
{
    for (unsigned i = 0; i < 256; i++)
        r[i] = a[i] + b[i];
}

static inline void s_mlkem_poly_sub_ref(int16_t *r, const int16_t *a, const int16_t *b)
{
    for (unsigned i = 0; i < 256; i++)
        r[i] = a[i] - b[i];
}

static inline void s_mlkem_cbd2_ref(int16_t *r, const uint8_t *buf)
{
    for (unsigned i = 0; i < 256 / 8; i++) {
        uint32_t t = (uint32_t)buf[4*i] | ((uint32_t)buf[4*i+1] << 8)
                   | ((uint32_t)buf[4*i+2] << 16) | ((uint32_t)buf[4*i+3] << 24);
        uint32_t d = (t & 0x55555555) + ((t >> 1) & 0x55555555);
        for (unsigned j = 0; j < 8; j++) {
            int16_t a = (int16_t)((d >> (4 * j))     & 0x3);
            int16_t b = (int16_t)((d >> (4 * j + 2)) & 0x3);
            r[8 * i + j] = a - b;
        }
    }
}

static inline void s_mlkem_cbd3_ref(int16_t *r, const uint8_t *buf)
{
    for (unsigned i = 0; i < 256 / 4; i++) {
        uint32_t t = (uint32_t)buf[3*i] | ((uint32_t)buf[3*i+1] << 8)
                   | ((uint32_t)buf[3*i+2] << 16);
        uint32_t d = (t & 0x00249249) + ((t >> 1) & 0x00249249) + ((t >> 2) & 0x00249249);
        for (unsigned j = 0; j < 4; j++) {
            int16_t a = (int16_t)((d >> (6 * j))     & 0x7);
            int16_t b = (int16_t)((d >> (6 * j + 3)) & 0x7);
            r[4 * i + j] = a - b;
        }
    }
}

/* ============================================================================
 *  Static inline dispatch — one cached branch, predicted for best arch.
 *  x86: check AVX2 once, predicted taken. ARM: NEON always-on, no branch.
 * ============================================================================ */


static inline void dap_mlkem_poly_csubq_fast(int16_t *a) {
#if defined(__x86_64__) || defined(_M_X64)
    static int s_ok = -1;
    if (__builtin_expect(s_ok < 0, 0))
        s_ok = (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX2);
    if (__builtin_expect(s_ok, 1))
        s_mlkem_poly_csubq_avx2(a);
    else
        s_mlkem_poly_csubq_ref(a);
#elif DAP_MLKEM_HAVE_NEON
    s_mlkem_poly_csubq_neon(a);
#else
    s_mlkem_poly_csubq_ref(a);
#endif
}

static inline void dap_mlkem_poly_reduce_fast(int16_t *a) {
#if defined(__x86_64__) || defined(_M_X64)
    static int s_ok = -1;
    if (__builtin_expect(s_ok < 0, 0))
        s_ok = (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX2);
    if (__builtin_expect(s_ok, 1))
        s_mlkem_poly_reduce_avx2(a);
    else
        s_mlkem_poly_reduce_ref(a);
#elif DAP_MLKEM_HAVE_NEON
    s_mlkem_poly_reduce_neon(a);
#else
    s_mlkem_poly_reduce_ref(a);
#endif
}

static inline void dap_mlkem_poly_tomont_fast(int16_t *a) {
#if defined(__x86_64__) || defined(_M_X64)
    static int s_ok = -1;
    if (__builtin_expect(s_ok < 0, 0))
        s_ok = (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX2);
    if (__builtin_expect(s_ok, 1))
        s_mlkem_poly_tomont_avx2(a);
    else
        s_mlkem_poly_tomont_ref(a);
#elif DAP_MLKEM_HAVE_NEON
    s_mlkem_poly_tomont_neon(a);
#else
    s_mlkem_poly_tomont_ref(a);
#endif
}



static inline void dap_mlkem_poly_add_fast(int16_t *r, const int16_t *a, const int16_t *b) {
#if defined(__x86_64__) || defined(_M_X64)
    static int s_ok = -1;
    if (__builtin_expect(s_ok < 0, 0))
        s_ok = (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX2);
    if (__builtin_expect(s_ok, 1))
        s_mlkem_poly_add_avx2(r, a, b);
    else
        s_mlkem_poly_add_ref(r, a, b);
#elif DAP_MLKEM_HAVE_NEON
    s_mlkem_poly_add_neon(r, a, b);
#else
    s_mlkem_poly_add_ref(r, a, b);
#endif
}

static inline void dap_mlkem_poly_sub_fast(int16_t *r, const int16_t *a, const int16_t *b) {
#if defined(__x86_64__) || defined(_M_X64)
    static int s_ok = -1;
    if (__builtin_expect(s_ok < 0, 0))
        s_ok = (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX2);
    if (__builtin_expect(s_ok, 1))
        s_mlkem_poly_sub_avx2(r, a, b);
    else
        s_mlkem_poly_sub_ref(r, a, b);
#elif DAP_MLKEM_HAVE_NEON
    s_mlkem_poly_sub_neon(r, a, b);
#else
    s_mlkem_poly_sub_ref(r, a, b);
#endif
}



static inline void dap_mlkem_cbd2_fast(int16_t *r, const uint8_t *buf) {
#if defined(__x86_64__) || defined(_M_X64)
    static int s_ok = -1;
    if (__builtin_expect(s_ok < 0, 0))
        s_ok = (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX2);
    if (__builtin_expect(s_ok, 1))
        s_mlkem_cbd2_avx2(r, buf);
    else
        s_mlkem_cbd2_ref(r, buf);
#elif DAP_MLKEM_HAVE_NEON
    s_mlkem_cbd2_neon(r, buf);
#else
    s_mlkem_cbd2_ref(r, buf);
#endif
}

static inline void dap_mlkem_cbd3_fast(int16_t *r, const uint8_t *buf) {
#if defined(__x86_64__) || defined(_M_X64)
    static int s_ok = -1;
    if (__builtin_expect(s_ok < 0, 0))
        s_ok = (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX2);
    if (__builtin_expect(s_ok, 1))
        s_mlkem_cbd3_avx2(r, buf);
    else
        s_mlkem_cbd3_ref(r, buf);
#elif DAP_MLKEM_HAVE_NEON
    s_mlkem_cbd3_neon(r, buf);
#else
    s_mlkem_cbd3_ref(r, buf);
#endif
}

