/**
 * @file dap_mlkem_polyvec.c
 * @brief Polynomial vector operations for ML-KEM (FIPS 203).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <string.h>
#include "dap_mlkem_polyvec.h"
#include "dap_mlkem_poly_simd.h"
#include "dap_cpu_arch.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define MLKEM_POLYVEC_AVX2 1
#endif

void MLKEM_NAMESPACE(_polyvec_compress)(uint8_t *a_r, dap_mlkem_polyvec *a_a)
{
    MLKEM_NAMESPACE(_polyvec_csubq)(a_a);
#if MLKEM_POLYVECCOMPRESSEDBYTES == (MLKEM_K * 352)
    for (unsigned i = 0; i < MLKEM_K; i++) {
        if (!MLKEM_SIMD_DISPATCH(compress_coeffs, a_a->vec[i].coeffs, 20158, 0x7FF)) {
            for (unsigned k = 0; k < MLKEM_N; k++)
                a_a->vec[i].coeffs[k] = (int16_t)((((uint32_t)a_a->vec[i].coeffs[k] << 11)
                    + MLKEM_Q / 2) / MLKEM_Q) & 0x7ff;
        }
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
        if (!MLKEM_SIMD_DISPATCH(compress_coeffs, a_a->vec[i].coeffs, 10079, 0x3FF)) {
            for (unsigned k = 0; k < MLKEM_N; k++)
                a_a->vec[i].coeffs[k] = (int16_t)((((uint32_t)a_a->vec[i].coeffs[k] << 10)
                    + MLKEM_Q / 2) / MLKEM_Q) & 0x3ff;
        }
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

MLKEM_HOTFN void MLKEM_NAMESPACE(_polyvec_pointwise_acc_montgomery)(dap_mlkem_poly *a_r,
                                                         const dap_mlkem_polyvec *a_a,
                                                         const dap_mlkem_polyvec *a_b)
{
    const int16_t *l_pa[MLKEM_K], *l_pb[MLKEM_K];
    for (unsigned i = 0; i < MLKEM_K; i++) {
        l_pa[i] = a_a->vec[i].coeffs;
        l_pb[i] = a_b->vec[i].coeffs;
    }
#if defined(__x86_64__) || defined(_M_X64)
    {
        extern void dap_mlkem_basemul_acc_asm(int16_t *, const int16_t * const *,
                                               const int16_t * const *, unsigned);
        if (dap_cpu_arch_get() >= DAP_CPU_ARCH_AVX512) {
            dap_mlkem_basemul_acc_asm(a_r->coeffs, l_pa, l_pb, MLKEM_K);
            return;
        }
    }
#endif
    if (MLKEM_SIMD_DISPATCH(basemul_acc_montgomery, a_r->coeffs, l_pa, l_pb, MLKEM_K))
        return;
    dap_mlkem_poly l_t;
    MLKEM_NAMESPACE(_poly_basemul_montgomery)(a_r, &a_a->vec[0], &a_b->vec[0]);
    for (unsigned i = 1; i < MLKEM_K; i++) {
        MLKEM_NAMESPACE(_poly_basemul_montgomery)(&l_t, &a_a->vec[i], &a_b->vec[i]);
        MLKEM_NAMESPACE(_poly_add)(a_r, a_r, &l_t);
    }
    MLKEM_NAMESPACE(_poly_reduce)(a_r);
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
