/**
 * @file dap_mlkem_poly.c
 * @brief Polynomial operations for ML-KEM (FIPS 203).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include "dap_mlkem_poly.h"
#include "dap_mlkem_ntt.h"
#include "dap_mlkem_reduce.h"
#include "dap_mlkem_cbd.h"
#include "dap_mlkem_symmetric.h"


void MLKEM_NAMESPACE(_poly_compress)(uint8_t *a_r, dap_mlkem_poly *a_a)
{
    MLKEM_NAMESPACE(_poly_csubq)(a_a);
#if MLKEM_POLYCOMPRESSEDBYTES == 128
    uint8_t t[8];
    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        for (unsigned j = 0; j < 8; j++)
            t[j] = (uint8_t)((((uint16_t)a_a->coeffs[8 * i + j] << 4) + MLKEM_Q / 2) / MLKEM_Q) & 15;
        a_r[0] = t[0] | (t[1] << 4);
        a_r[1] = t[2] | (t[3] << 4);
        a_r[2] = t[4] | (t[5] << 4);
        a_r[3] = t[6] | (t[7] << 4);
        a_r += 4;
    }
#elif MLKEM_POLYCOMPRESSEDBYTES == 160
    uint8_t t[8];
    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        for (unsigned j = 0; j < 8; j++)
            t[j] = (uint8_t)((((uint32_t)a_a->coeffs[8 * i + j] << 5) + MLKEM_Q / 2) / MLKEM_Q) & 31;
        a_r[0] = (t[0]) | (t[1] << 5);
        a_r[1] = (t[1] >> 3) | (t[2] << 2) | (t[3] << 7);
        a_r[2] = (t[3] >> 1) | (t[4] << 4);
        a_r[3] = (t[4] >> 4) | (t[5] << 1) | (t[6] << 6);
        a_r[4] = (t[6] >> 2) | (t[7] << 3);
        a_r += 5;
    }
#endif
}

void MLKEM_NAMESPACE(_poly_decompress)(dap_mlkem_poly *a_r, const uint8_t *a_a)
{
#if MLKEM_POLYCOMPRESSEDBYTES == 128
    for (unsigned i = 0; i < MLKEM_N / 2; i++) {
        a_r->coeffs[2 * i]     = (int16_t)(((uint16_t)(a_a[0] & 15) * MLKEM_Q + 8) >> 4);
        a_r->coeffs[2 * i + 1] = (int16_t)(((uint16_t)(a_a[0] >> 4) * MLKEM_Q + 8) >> 4);
        a_a++;
    }
#elif MLKEM_POLYCOMPRESSEDBYTES == 160
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

void MLKEM_NAMESPACE(_poly_tobytes)(uint8_t *a_r, dap_mlkem_poly *a_a)
{
    MLKEM_NAMESPACE(_poly_csubq)(a_a);
    for (unsigned i = 0; i < MLKEM_N / 2; i++) {
        uint16_t t0 = (uint16_t)a_a->coeffs[2 * i];
        uint16_t t1 = (uint16_t)a_a->coeffs[2 * i + 1];
        a_r[3 * i]     = (uint8_t)(t0);
        a_r[3 * i + 1] = (uint8_t)((t0 >> 8) | (t1 << 4));
        a_r[3 * i + 2] = (uint8_t)(t1 >> 4);
    }
}

void MLKEM_NAMESPACE(_poly_frombytes)(dap_mlkem_poly *a_r, const uint8_t *a_a)
{
    for (unsigned i = 0; i < MLKEM_N / 2; i++) {
        a_r->coeffs[2 * i]     = (int16_t)(((a_a[3 * i]) | ((uint16_t)a_a[3 * i + 1] << 8)) & 0xFFF);
        a_r->coeffs[2 * i + 1] = (int16_t)(((a_a[3 * i + 1] >> 4) | ((uint16_t)a_a[3 * i + 2] << 4)) & 0xFFF);
    }
}

void MLKEM_NAMESPACE(_poly_frommsg)(dap_mlkem_poly *a_r, const uint8_t a_msg[MLKEM_INDCPA_MSGBYTES])
{
    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        for (unsigned j = 0; j < 8; j++) {
            int16_t mask = -(int16_t)((a_msg[i] >> j) & 1);
            a_r->coeffs[8 * i + j] = mask & ((MLKEM_Q + 1) / 2);
        }
    }
}

void MLKEM_NAMESPACE(_poly_tomsg)(uint8_t a_msg[MLKEM_INDCPA_MSGBYTES], dap_mlkem_poly *a_a)
{
    MLKEM_NAMESPACE(_poly_csubq)(a_a);
    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        a_msg[i] = 0;
        for (unsigned j = 0; j < 8; j++) {
            uint16_t t = (uint16_t)((((uint16_t)a_a->coeffs[8 * i + j] << 1) + MLKEM_Q / 2) / MLKEM_Q) & 1;
            a_msg[i] |= (uint8_t)(t << j);
        }
    }
}

void MLKEM_NAMESPACE(_poly_getnoise_eta1)(dap_mlkem_poly *a_r,
                                           const uint8_t a_seed[MLKEM_SYMBYTES], uint8_t a_nonce)
{
    uint8_t l_buf[MLKEM_ETA1 * MLKEM_N / 4];
    dap_mlkem_prf(l_buf, sizeof(l_buf), a_seed, a_nonce);
    MLKEM_NAMESPACE(_cbd_eta1)(a_r, l_buf);
}

void MLKEM_NAMESPACE(_poly_getnoise_eta2)(dap_mlkem_poly *a_r,
                                           const uint8_t a_seed[MLKEM_SYMBYTES], uint8_t a_nonce)
{
    uint8_t l_buf[MLKEM_ETA2 * MLKEM_N / 4];
    dap_mlkem_prf(l_buf, sizeof(l_buf), a_seed, a_nonce);
    MLKEM_NAMESPACE(_cbd_eta2)(a_r, l_buf);
}

void MLKEM_NAMESPACE(_poly_ntt)(dap_mlkem_poly *a_r)
{
    MLKEM_NAMESPACE(_ntt)(a_r->coeffs);
    MLKEM_NAMESPACE(_poly_reduce)(a_r);
}

void MLKEM_NAMESPACE(_poly_invntt_tomont)(dap_mlkem_poly *a_r)
{
    MLKEM_NAMESPACE(_invntt)(a_r->coeffs);
}

void MLKEM_NAMESPACE(_poly_basemul_montgomery)(dap_mlkem_poly *a_r,
                                                const dap_mlkem_poly *a_a,
                                                const dap_mlkem_poly *a_b)
{
    const int16_t *l_zetas = MLKEM_NAMESPACE(_get_zetas)();
    for (unsigned i = 0; i < MLKEM_N / 4; i++) {
        MLKEM_NAMESPACE(_basemul)(&a_r->coeffs[4 * i],
                                   &a_a->coeffs[4 * i],
                                   &a_b->coeffs[4 * i],
                                   l_zetas[64 + i]);
        MLKEM_NAMESPACE(_basemul)(&a_r->coeffs[4 * i + 2],
                                   &a_a->coeffs[4 * i + 2],
                                   &a_b->coeffs[4 * i + 2],
                                   (int16_t)(-l_zetas[64 + i]));
    }
}

void MLKEM_NAMESPACE(_poly_tomont)(dap_mlkem_poly *a_r)
{
    const int16_t f = (int16_t)((1ULL << 32) % MLKEM_Q);
    for (unsigned i = 0; i < MLKEM_N; i++)
        a_r->coeffs[i] = dap_mlkem_montgomery_reduce((int32_t)a_r->coeffs[i] * f);
}

void MLKEM_NAMESPACE(_poly_reduce)(dap_mlkem_poly *a_r)
{
    for (unsigned i = 0; i < MLKEM_N; i++)
        a_r->coeffs[i] = dap_mlkem_barrett_reduce(a_r->coeffs[i]);
}

void MLKEM_NAMESPACE(_poly_csubq)(dap_mlkem_poly *a_r)
{
    for (unsigned i = 0; i < MLKEM_N; i++)
        a_r->coeffs[i] = dap_mlkem_csubq(a_r->coeffs[i]);
}

void MLKEM_NAMESPACE(_poly_add)(dap_mlkem_poly *a_r, const dap_mlkem_poly *a_a,
                                 const dap_mlkem_poly *a_b)
{
    for (unsigned i = 0; i < MLKEM_N; i++)
        a_r->coeffs[i] = a_a->coeffs[i] + a_b->coeffs[i];
}

void MLKEM_NAMESPACE(_poly_sub)(dap_mlkem_poly *a_r, const dap_mlkem_poly *a_a,
                                 const dap_mlkem_poly *a_b)
{
    for (unsigned i = 0; i < MLKEM_N; i++)
        a_r->coeffs[i] = a_a->coeffs[i] - a_b->coeffs[i];
}
