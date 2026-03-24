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
 *  AArch64 — NEON section
 * ============================================================================ */

#if defined(__aarch64__) || defined(_M_ARM64)
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

#endif /* aarch64 */

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
    for (unsigned i = 0; i < 256; i++)
        a[i] = dap_mlkem_barrett_reduce(a[i]);
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
#elif defined(__aarch64__) || defined(_M_ARM64)
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
#elif defined(__aarch64__) || defined(_M_ARM64)
    s_mlkem_poly_{{fn}}_neon(r, a, b);
#else
    s_mlkem_poly_{{fn}}_ref(r, a, b);
#endif
}
{{/for}}
