/**
 * @file dap_mlkem_poly.c
 * @brief Polynomial operations for ML-KEM (FIPS 203).
 *
 * SIMD operations dispatched via DAP_DISPATCH_LOCAL — one resolved indirect
 * call per function after init. Generated variants in separate TUs from
 * dap_mlkem_poly_simd.c.tpl and dap_mlkem_poly_ops_simd.c.tpl.
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
#include "dap_arch_dispatch.h"

/* ============================================================================
 * Dispatch pointers for poly I/O operations
 * ============================================================================ */

DAP_DISPATCH_LOCAL(s_compress_d4,      void, uint8_t *, const int16_t *);
DAP_DISPATCH_LOCAL(s_compress_d5,      void, uint8_t *, const int16_t *);
DAP_DISPATCH_LOCAL(s_decompress_d4,    void, int16_t *, const uint8_t *);
DAP_DISPATCH_LOCAL(s_decompress_d5,    void, int16_t *, const uint8_t *);
DAP_DISPATCH_LOCAL(s_tobytes,          void, uint8_t *, const int16_t *);
DAP_DISPATCH_LOCAL(s_frombytes,        void, int16_t *, const uint8_t *);
DAP_DISPATCH_LOCAL(s_frommsg,          void, int16_t *, const uint8_t *);
DAP_DISPATCH_LOCAL(s_tomsg,            void, uint8_t *, const int16_t *);
DAP_DISPATCH_LOCAL(s_mulcache_compute, void, int16_t * restrict, const int16_t * restrict);

DAP_DISPATCH_LOCAL(s_poly_basemul, void, int16_t *, const int16_t *, const int16_t *, const int16_t *);

/* ============================================================================
 * Scalar reference implementations
 * ============================================================================ */

static void s_compress_d4_ref(uint8_t *a_r, const int16_t *a_coeffs)
{
    for (unsigned i = 0; i < MLKEM_N / 2; i++) {
        uint8_t c0 = (uint8_t)((((uint16_t)a_coeffs[2 * i]     << 4) + MLKEM_Q / 2) / MLKEM_Q);
        uint8_t c1 = (uint8_t)((((uint16_t)a_coeffs[2 * i + 1] << 4) + MLKEM_Q / 2) / MLKEM_Q);
        a_r[i] = (c0 & 0xF) | ((c1 & 0xF) << 4);
    }
}

static void s_compress_d5_ref(uint8_t *a_r, const int16_t *a_coeffs)
{
    int16_t t[MLKEM_N];
    for (unsigned k = 0; k < MLKEM_N; k++)
        t[k] = (int16_t)((((uint32_t)a_coeffs[k] << 5) + MLKEM_Q / 2) / MLKEM_Q) & 31;
    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        const int16_t *c = t + 8 * i;
        a_r[0] = (uint8_t)((c[0]) | (c[1] << 5));
        a_r[1] = (uint8_t)((c[1] >> 3) | (c[2] << 2) | (c[3] << 7));
        a_r[2] = (uint8_t)((c[3] >> 1) | (c[4] << 4));
        a_r[3] = (uint8_t)((c[4] >> 4) | (c[5] << 1) | (c[6] << 6));
        a_r[4] = (uint8_t)((c[6] >> 2) | (c[7] << 3));
        a_r += 5;
    }
}

static void s_decompress_d4_ref(int16_t *a_r, const uint8_t *a_a)
{
    for (unsigned i = 0; i < MLKEM_N / 2; i++) {
        a_r[2 * i]     = (int16_t)(((uint16_t)(a_a[i] & 15) * MLKEM_Q + 8) >> 4);
        a_r[2 * i + 1] = (int16_t)(((uint16_t)(a_a[i] >> 4) * MLKEM_Q + 8) >> 4);
    }
}

static void s_decompress_d5_ref(int16_t *a_r, const uint8_t *a_a)
{
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
            a_r[8 * i + j] = (int16_t)(((uint32_t)(t[j] & 31) * MLKEM_Q + 16) >> 5);
    }
}

static void s_tobytes_ref(uint8_t *a_r, const int16_t *a_coeffs)
{
    for (unsigned i = 0; i < MLKEM_N / 2; i++) {
        uint16_t t0 = (uint16_t)a_coeffs[2 * i];
        uint16_t t1 = (uint16_t)a_coeffs[2 * i + 1];
        a_r[3 * i]     = (uint8_t)(t0);
        a_r[3 * i + 1] = (uint8_t)((t0 >> 8) | (t1 << 4));
        a_r[3 * i + 2] = (uint8_t)(t1 >> 4);
    }
}

static void s_frombytes_ref(int16_t *a_r, const uint8_t *a_a)
{
    for (unsigned i = 0; i < MLKEM_N / 2; i++) {
        a_r[2 * i]     = (int16_t)(((a_a[3 * i]) | ((uint16_t)a_a[3 * i + 1] << 8)) & 0xFFF);
        a_r[2 * i + 1] = (int16_t)(((a_a[3 * i + 1] >> 4) | ((uint16_t)a_a[3 * i + 2] << 4)) & 0xFFF);
    }
}

static void s_frommsg_ref(int16_t *a_r, const uint8_t *a_msg)
{
    for (unsigned i = 0; i < MLKEM_N / 8; i++)
        for (unsigned j = 0; j < 8; j++) {
            int16_t mask = -(int16_t)((a_msg[i] >> j) & 1);
            a_r[8 * i + j] = mask & ((MLKEM_Q + 1) / 2);
        }
}

static void s_tomsg_ref(uint8_t *a_msg, const int16_t *a_coeffs)
{
    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        a_msg[i] = 0;
        for (unsigned j = 0; j < 8; j++) {
            uint16_t t = (uint16_t)((((uint16_t)a_coeffs[8 * i + j] << 1) + MLKEM_Q / 2) / MLKEM_Q) & 1;
            a_msg[i] |= (uint8_t)(t << j);
        }
    }
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

static void s_mulcache_compute_ref(int16_t * restrict a_cache, const int16_t * restrict a_b)
{
    const int16_t *l_z = s_basemul_zetas_nttpack;
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        const int16_t *l_be = a_b + 32 * l_p;
        const int16_t *l_bo = a_b + 32 * l_p + 16;
        int16_t *l_ce = a_cache + 32 * l_p;
        int16_t *l_co = a_cache + 32 * l_p + 16;
        for (unsigned l_j = 0; l_j < 16; l_j++) {
            l_ce[l_j] = l_be[l_j];
            l_co[l_j] = dap_mlkem_fqmul(l_bo[l_j], l_z[16 * l_p + l_j]);
        }
    }
}

static void s_poly_basemul_ref(int16_t *a_r, const int16_t *a_a, const int16_t *a_b, const int16_t *a_z)
{
    (void)a_z;
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        const int16_t *l_ae = a_a + 32 * l_p;
        const int16_t *l_ao = a_a + 32 * l_p + 16;
        const int16_t *l_be = a_b + 32 * l_p;
        const int16_t *l_bo = a_b + 32 * l_p + 16;
        int16_t *l_re = a_r + 32 * l_p;
        int16_t *l_ro = a_r + 32 * l_p + 16;
        const int16_t *l_z = s_basemul_zetas_nttpack + 16 * l_p;
        for (unsigned l_j = 0; l_j < 16; l_j++) {
            l_re[l_j] = dap_mlkem_fqmul(l_ae[l_j], l_be[l_j])
                       + dap_mlkem_fqmul(dap_mlkem_fqmul(l_ao[l_j], l_bo[l_j]), l_z[l_j]);
            l_ro[l_j] = dap_mlkem_fqmul(l_ae[l_j], l_bo[l_j])
                       + dap_mlkem_fqmul(l_ao[l_j], l_be[l_j]);
        }
    }
}

/* ============================================================================
 * Dispatch init — resolved once, cached thereafter
 * ============================================================================ */

static void s_mlkem_poly_dispatch_init(void)
{
    dap_algo_class_t l_class = dap_algo_class_register("MLKEM_POLY");

    DAP_DISPATCH_DEFAULT(s_compress_d4,      s_compress_d4_ref);
    DAP_DISPATCH_DEFAULT(s_compress_d5,      s_compress_d5_ref);
    DAP_DISPATCH_DEFAULT(s_decompress_d4,    s_decompress_d4_ref);
    DAP_DISPATCH_DEFAULT(s_decompress_d5,    s_decompress_d5_ref);
    DAP_DISPATCH_DEFAULT(s_tobytes,          s_tobytes_ref);
    DAP_DISPATCH_DEFAULT(s_frombytes,        s_frombytes_ref);
    DAP_DISPATCH_DEFAULT(s_frommsg,          s_frommsg_ref);
    DAP_DISPATCH_DEFAULT(s_tomsg,            s_tomsg_ref);
    DAP_DISPATCH_DEFAULT(s_mulcache_compute, s_mulcache_compute_ref);
    DAP_DISPATCH_DEFAULT(s_poly_basemul,     s_poly_basemul_ref);

    DAP_DISPATCH_ARCH_SELECT_FOR(l_class);

    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_compress_d4,      dap_mlkem_poly_compress_d4_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_compress_d5,      dap_mlkem_poly_compress_d5_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_decompress_d4,    dap_mlkem_poly_decompress_d4_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_decompress_d5,    dap_mlkem_poly_decompress_d5_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_tobytes,          dap_mlkem_poly_tobytes_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_frombytes,        dap_mlkem_poly_frombytes_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_frommsg,          dap_mlkem_poly_frommsg_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_tomsg,            dap_mlkem_poly_tomsg_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_mulcache_compute, dap_mlkem_poly_mulcache_compute_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_poly_basemul,     dap_mlkem_poly_basemul_montgomery_avx2);

#if !defined(__APPLE__)
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, s_compress_d4,      dap_mlkem_poly_compress_d4_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, s_compress_d5,      dap_mlkem_poly_compress_d5_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, s_decompress_d4,    dap_mlkem_poly_decompress_d4_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, s_decompress_d5,    dap_mlkem_poly_decompress_d5_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, s_tobytes,          dap_mlkem_poly_tobytes_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, s_frombytes,        dap_mlkem_poly_frombytes_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, s_frommsg,          dap_mlkem_poly_frommsg_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, s_tomsg,            dap_mlkem_poly_tomsg_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, s_mulcache_compute, dap_mlkem_poly_mulcache_compute_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON, s_poly_basemul,     dap_mlkem_poly_basemul_montgomery_neon);
#endif
}

#define POLY_ENSURE() DAP_DISPATCH_ENSURE(s_compress_d4, s_mlkem_poly_dispatch_init)

/* ============================================================================
 * Public API — thin wrappers over dispatch pointers
 * ============================================================================ */

void MLKEM_NAMESPACE(_poly_compress)(uint8_t *a_r, dap_mlkem_poly *a_a)
{
    POLY_ENSURE();
#if MLKEM_POLYCOMPRESSEDBYTES == 128
    s_compress_d4_ptr(a_r, a_a->coeffs);
#elif MLKEM_POLYCOMPRESSEDBYTES == 160
    s_compress_d5_ptr(a_r, a_a->coeffs);
#endif
}

void MLKEM_NAMESPACE(_poly_decompress)(dap_mlkem_poly *a_r, const uint8_t *a_a)
{
    POLY_ENSURE();
#if MLKEM_POLYCOMPRESSEDBYTES == 128
    s_decompress_d4_ptr(a_r->coeffs, a_a);
#elif MLKEM_POLYCOMPRESSEDBYTES == 160
    s_decompress_d5_ptr(a_r->coeffs, a_a);
#endif
}

void MLKEM_NAMESPACE(_poly_tobytes)(uint8_t *a_r, dap_mlkem_poly *a_a)
{
    POLY_ENSURE();
    s_tobytes_ptr(a_r, a_a->coeffs);
}

void MLKEM_NAMESPACE(_poly_frombytes)(dap_mlkem_poly *a_r, const uint8_t *a_a)
{
    POLY_ENSURE();
    s_frombytes_ptr(a_r->coeffs, a_a);
}

void MLKEM_NAMESPACE(_poly_frommsg)(dap_mlkem_poly *a_r, const uint8_t a_msg[MLKEM_INDCPA_MSGBYTES])
{
    POLY_ENSURE();
    s_frommsg_ptr(a_r->coeffs, a_msg);
}

void MLKEM_NAMESPACE(_poly_tomsg)(uint8_t a_msg[MLKEM_INDCPA_MSGBYTES], dap_mlkem_poly *a_a)
{
    POLY_ENSURE();
    s_tomsg_ptr(a_msg, a_a->coeffs);
}

void MLKEM_NAMESPACE(_poly_getnoise_eta1)(dap_mlkem_poly *a_r,
                                           const uint8_t a_seed[MLKEM_SYMBYTES], uint8_t a_nonce)
{
    enum { DATA_LEN = MLKEM_ETA1 * MLKEM_N / 4, SIMD_PAD = 8 };
    uint8_t l_buf[DATA_LEN + SIMD_PAD];
    dap_mlkem_prf(l_buf, DATA_LEN, a_seed, a_nonce);
    MLKEM_NAMESPACE(_cbd_eta1)(a_r, l_buf);
}

void MLKEM_NAMESPACE(_poly_getnoise_eta1_x4)(dap_mlkem_poly *a_r0, dap_mlkem_poly *a_r1,
                                               dap_mlkem_poly *a_r2, dap_mlkem_poly *a_r3,
                                               const uint8_t a_seed[MLKEM_SYMBYTES],
                                               uint8_t a_n0, uint8_t a_n1,
                                               uint8_t a_n2, uint8_t a_n3)
{
    enum { DATA_LEN = MLKEM_ETA1 * MLKEM_N / 4, SIMD_PAD = 8 };
    uint8_t l_buf[4][DATA_LEN + SIMD_PAD];
    dap_mlkem_prf_x4(l_buf[0], l_buf[1], l_buf[2], l_buf[3],
                      DATA_LEN, a_seed, a_n0, a_n1, a_n2, a_n3);
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
    POLY_ENSURE();
    s_poly_basemul_ptr(a_r->coeffs, a_a->coeffs, a_b->coeffs, NULL);
}

void MLKEM_NAMESPACE(_poly_tomont)(dap_mlkem_poly *a_r)
{
    dap_mlkem_poly_tomont_fast(a_r->coeffs);
}

void MLKEM_NAMESPACE(_poly_reduce)(dap_mlkem_poly *a_r)
{
    dap_mlkem_poly_reduce_fast(a_r->coeffs);
}

void MLKEM_NAMESPACE(_poly_csubq)(dap_mlkem_poly *a_r)
{
    dap_mlkem_poly_csubq_fast(a_r->coeffs);
}

MLKEM_HOTFN void MLKEM_NAMESPACE(_poly_add)(dap_mlkem_poly *a_r, const dap_mlkem_poly *a_a,
                                              const dap_mlkem_poly *a_b)
{
    dap_mlkem_poly_add_fast(a_r->coeffs, a_a->coeffs, a_b->coeffs);
}

MLKEM_HOTFN void MLKEM_NAMESPACE(_poly_sub)(dap_mlkem_poly *a_r, const dap_mlkem_poly *a_a,
                                              const dap_mlkem_poly *a_b)
{
    dap_mlkem_poly_sub_fast(a_r->coeffs, a_a->coeffs, a_b->coeffs);
}

void MLKEM_NAMESPACE(_poly_mulcache_compute)(dap_mlkem_poly_mulcache *a_cache,
                                              const dap_mlkem_poly *a_b)
{
    POLY_ENSURE();
    s_mulcache_compute_ptr(a_cache->coeffs, a_b->coeffs);
}
