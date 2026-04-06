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

/* AArch64 hosts, or ARMv7 with NEON (Android armeabi-v7a).
 * Apple Silicon: disable inline NEON fast paths until parity is proven on all
 * Xcode/clang combos (shared-secret ML-KEM failures in CI vs Linux AArch64). */
#if (defined(__aarch64__) || defined(_M_ARM64) || (defined(__ARM_NEON) && defined(__arm__))) \
    && !defined(__APPLE__)
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

{{#include AVX2_FAST_BODIES}}

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
{{#include NEON_FAST_BODIES}}

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

{{#for fn in csubq|reduce|tomont}}
static inline void dap_mlkem_poly_{{fn}}_fast(int16_t *a) {
#if defined(__x86_64__) || defined(_M_X64)
    static int s_ok = -1;
    if (__builtin_expect(s_ok < 0, 0))
        s_ok = (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX2);
    if (__builtin_expect(s_ok, 1))
        s_mlkem_poly_{{fn}}_avx2(a);
    else
        s_mlkem_poly_{{fn}}_ref(a);
#elif DAP_MLKEM_HAVE_NEON
    s_mlkem_poly_{{fn}}_neon(a);
#else
    s_mlkem_poly_{{fn}}_ref(a);
#endif
}
{{/for}}

{{#for fn in add|sub}}
static inline void dap_mlkem_poly_{{fn}}_fast(int16_t *r, const int16_t *a, const int16_t *b) {
#if defined(__x86_64__) || defined(_M_X64)
    static int s_ok = -1;
    if (__builtin_expect(s_ok < 0, 0))
        s_ok = (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX2);
    if (__builtin_expect(s_ok, 1))
        s_mlkem_poly_{{fn}}_avx2(r, a, b);
    else
        s_mlkem_poly_{{fn}}_ref(r, a, b);
#elif DAP_MLKEM_HAVE_NEON
    s_mlkem_poly_{{fn}}_neon(r, a, b);
#else
    s_mlkem_poly_{{fn}}_ref(r, a, b);
#endif
}
{{/for}}

{{#for fn in cbd2|cbd3}}
static inline void dap_mlkem_{{fn}}_fast(int16_t *r, const uint8_t *buf) {
#if defined(__x86_64__) || defined(_M_X64)
    static int s_ok = -1;
    if (__builtin_expect(s_ok < 0, 0))
        s_ok = (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX2);
    if (__builtin_expect(s_ok, 1))
        s_mlkem_{{fn}}_avx2(r, buf);
    else
        s_mlkem_{{fn}}_ref(r, buf);
#elif DAP_MLKEM_HAVE_NEON
    s_mlkem_{{fn}}_neon(r, buf);
#else
    s_mlkem_{{fn}}_ref(r, buf);
#endif
}
{{/for}}
