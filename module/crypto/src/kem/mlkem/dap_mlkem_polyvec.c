/**
 * @file dap_mlkem_polyvec.c
 * @brief Polynomial vector operations for ML-KEM (FIPS 203).
 *
 * SIMD operations dispatched via DAP_DISPATCH_LOCAL. Generated variants
 * from dap_mlkem_polyvec_simd.c.tpl (compress, decompress, basemul_acc_cached).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <string.h>
#include "dap_mlkem_polyvec.h"
#include "dap_mlkem_ntt.h"
#include "dap_mlkem_poly_simd.h"
#include "dap_arch_dispatch.h"

/* ============================================================================
 * Dispatch pointers for polyvec SIMD operations
 * ============================================================================ */

DAP_DISPATCH_LOCAL(s_pv_compress_d10,   void, uint8_t *, const int16_t *);
DAP_DISPATCH_LOCAL(s_pv_compress_d11,   void, uint8_t *, const int16_t *);
DAP_DISPATCH_LOCAL(s_pv_decompress_d10, void, int16_t *, const uint8_t *);
DAP_DISPATCH_LOCAL(s_pv_decompress_d11, void, int16_t *, const uint8_t *);
DAP_DISPATCH_LOCAL(s_pv_basemul_acc,    void, int16_t * restrict, const int16_t * const *, const int16_t * const *, const int16_t * const *, unsigned);

/* ============================================================================
 * Scalar reference implementations
 * ============================================================================ */

static void s_pv_compress_d11_ref(uint8_t *a_r, const int16_t *a_coeffs)
{
    int16_t t[MLKEM_N];
    for (unsigned k = 0; k < MLKEM_N; k++)
        t[k] = (int16_t)((((uint32_t)a_coeffs[k] << 11) + MLKEM_Q / 2) / MLKEM_Q) & 0x7ff;
    for (unsigned j = 0; j < MLKEM_N / 8; j++) {
        const int16_t *c = t + 8 * j;
        a_r[ 0] = (uint8_t)(c[0]);
        a_r[ 1] = (uint8_t)((c[0] >> 8) | (c[1] << 3));
        a_r[ 2] = (uint8_t)((c[1] >> 5) | (c[2] << 6));
        a_r[ 3] = (uint8_t)(c[2] >> 2);
        a_r[ 4] = (uint8_t)((c[2] >> 10) | (c[3] << 1));
        a_r[ 5] = (uint8_t)((c[3] >> 7) | (c[4] << 4));
        a_r[ 6] = (uint8_t)((c[4] >> 4) | (c[5] << 7));
        a_r[ 7] = (uint8_t)(c[5] >> 1);
        a_r[ 8] = (uint8_t)((c[5] >> 9) | (c[6] << 2));
        a_r[ 9] = (uint8_t)((c[6] >> 6) | (c[7] << 5));
        a_r[10] = (uint8_t)(c[7] >> 3);
        a_r += 11;
    }
}

static void s_pv_compress_d10_ref(uint8_t *a_r, const int16_t *a_coeffs)
{
    int16_t t[MLKEM_N];
    for (unsigned k = 0; k < MLKEM_N; k++)
        t[k] = (int16_t)((((uint32_t)a_coeffs[k] << 10) + MLKEM_Q / 2) / MLKEM_Q) & 0x3ff;
    for (unsigned j = 0; j < MLKEM_N / 4; j++) {
        const int16_t *c = t + 4 * j;
        a_r[0] = (uint8_t)(c[0]);
        a_r[1] = (uint8_t)((c[0] >> 8) | (c[1] << 2));
        a_r[2] = (uint8_t)((c[1] >> 6) | (c[2] << 4));
        a_r[3] = (uint8_t)((c[2] >> 4) | (c[3] << 6));
        a_r[4] = (uint8_t)(c[3] >> 2);
        a_r += 5;
    }
}

static void s_pv_decompress_d11_ref(int16_t *a_r, const uint8_t *a_a)
{
    uint16_t t[8];
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
            a_r[8 * j + k] = (int16_t)(((uint32_t)(t[k] & 0x7FF) * MLKEM_Q + 1024) >> 11);
    }
}

static void s_pv_decompress_d10_ref(int16_t *a_r, const uint8_t *a_a)
{
    uint16_t t[4];
    for (unsigned j = 0; j < MLKEM_N / 4; j++) {
        t[0] = (a_a[0])       | ((uint16_t)a_a[1] << 8);
        t[1] = (a_a[1] >> 2)  | ((uint16_t)a_a[2] << 6);
        t[2] = (a_a[2] >> 4)  | ((uint16_t)a_a[3] << 4);
        t[3] = (a_a[3] >> 6)  | ((uint16_t)a_a[4] << 2);
        a_a += 5;
        for (unsigned k = 0; k < 4; k++)
            a_r[4 * j + k] = (int16_t)(((uint32_t)(t[k] & 0x3FF) * MLKEM_Q + 512) >> 10);
    }
}

static void s_pv_basemul_acc_ref(
    int16_t * restrict a_r,
    const int16_t * const *a_polys_a,
    const int16_t * const *a_polys_b,
    const int16_t * const *a_caches,
    unsigned a_count)
{
    (void)a_caches;
    dap_mlkem_poly l_r, l_t;
    dap_mlkem_poly l_aa, l_bb;
    memcpy(l_aa.coeffs, a_polys_a[0], sizeof(l_aa.coeffs));
    memcpy(l_bb.coeffs, a_polys_b[0], sizeof(l_bb.coeffs));
    MLKEM_NAMESPACE(_poly_basemul_montgomery)(&l_r, &l_aa, &l_bb);
    for (unsigned i = 1; i < a_count; i++) {
        memcpy(l_aa.coeffs, a_polys_a[i], sizeof(l_aa.coeffs));
        memcpy(l_bb.coeffs, a_polys_b[i], sizeof(l_bb.coeffs));
        MLKEM_NAMESPACE(_poly_basemul_montgomery)(&l_t, &l_aa, &l_bb);
        MLKEM_NAMESPACE(_poly_add)(&l_r, &l_r, &l_t);
    }
    MLKEM_NAMESPACE(_poly_reduce)(&l_r);
    memcpy(a_r, l_r.coeffs, sizeof(l_r.coeffs));
}

/* ============================================================================
 * Dispatch init
 * ============================================================================ */

static void s_mlkem_polyvec_dispatch_init(void)
{
    DAP_DISPATCH_DEFAULT(s_pv_compress_d10,   s_pv_compress_d10_ref);
    DAP_DISPATCH_DEFAULT(s_pv_compress_d11,   s_pv_compress_d11_ref);
    DAP_DISPATCH_DEFAULT(s_pv_decompress_d10, s_pv_decompress_d10_ref);
    DAP_DISPATCH_DEFAULT(s_pv_decompress_d11, s_pv_decompress_d11_ref);
    DAP_DISPATCH_DEFAULT(s_pv_basemul_acc,    s_pv_basemul_acc_ref);

    DAP_DISPATCH_ARCH_SELECT;

    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_pv_compress_d10,   dap_mlkem_polyvec_compress_d10_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_pv_compress_d11,   dap_mlkem_polyvec_compress_d11_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_pv_decompress_d10, dap_mlkem_polyvec_decompress_d10_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_pv_decompress_d11, dap_mlkem_polyvec_decompress_d11_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_pv_basemul_acc,    dap_mlkem_polyvec_basemul_acc_cached_avx2);

    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, s_pv_compress_d10,   dap_mlkem_polyvec_compress_d10_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, s_pv_compress_d11,   dap_mlkem_polyvec_compress_d11_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, s_pv_decompress_d10, dap_mlkem_polyvec_decompress_d10_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, s_pv_decompress_d11, dap_mlkem_polyvec_decompress_d11_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, s_pv_basemul_acc,    dap_mlkem_polyvec_basemul_acc_cached_neon);
}

#define PV_ENSURE() DAP_DISPATCH_ENSURE(s_pv_compress_d10, s_mlkem_polyvec_dispatch_init)

/* ============================================================================
 * Public API
 * ============================================================================ */

void MLKEM_NAMESPACE(_polyvec_compress)(uint8_t *a_r, dap_mlkem_polyvec *a_a)
{
    PV_ENSURE();
    MLKEM_NAMESPACE(_polyvec_csubq)(a_a);
    for (unsigned i = 0; i < MLKEM_K; i++) {
#if MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 352)
        s_pv_compress_d11_ptr(a_r, a_a->vec[i].coeffs);
        a_r += 352;
#elif MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 320)
        s_pv_compress_d10_ptr(a_r, a_a->vec[i].coeffs);
        a_r += 320;
#endif
    }
}

MLKEM_HOTFN void MLKEM_NAMESPACE(_polyvec_decompress)(dap_mlkem_polyvec *a_r, const uint8_t *a_a)
{
    PV_ENSURE();
    for (unsigned i = 0; i < MLKEM_K; i++) {
#if MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 352)
        s_pv_decompress_d11_ptr(a_r->vec[i].coeffs, a_a);
        a_a += 352;
#elif MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 320)
        s_pv_decompress_d10_ptr(a_r->vec[i].coeffs, a_a);
        a_a += 320;
#endif
    }
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

MLKEM_HOTFN void MLKEM_NAMESPACE(_polyvec_basemul_acc_montgomery_cached)(
    dap_mlkem_poly *a_r,
    const dap_mlkem_polyvec *a_a,
    const dap_mlkem_polyvec *a_b,
    const dap_mlkem_polyvec_mulcache *a_b_cache)
{
    PV_ENSURE();
    const int16_t *l_pa[MLKEM_K], *l_pb[MLKEM_K], *l_pc[MLKEM_K];
    for (unsigned i = 0; i < MLKEM_K; i++) {
        l_pa[i] = a_a->vec[i].coeffs;
        l_pb[i] = a_b->vec[i].coeffs;
        l_pc[i] = a_b_cache->vec[i].coeffs;
    }
    s_pv_basemul_acc_ptr(a_r->coeffs, l_pa, l_pb, l_pc, MLKEM_K);
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
