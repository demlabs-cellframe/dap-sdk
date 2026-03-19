/**
 * @file dap_mlkem_indcpa.c
 * @brief IND-CPA public-key encryption for ML-KEM (FIPS 203).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <string.h>
#include "dap_mlkem_indcpa.h"
#include "dap_mlkem_poly.h"
#include "dap_mlkem_polyvec.h"
#include "dap_mlkem_symmetric.h"
#include "dap_rand.h"

/* ---- pack / unpack ---- */

static void s_pack_pk(uint8_t a_r[MLKEM_INDCPA_PUBLICKEYBYTES],
                       dap_mlkem_polyvec *a_pk,
                       const uint8_t a_seed[MLKEM_SYMBYTES])
{
    MLKEM_NAMESPACE(_polyvec_tobytes)(a_r, a_pk);
    memcpy(a_r + MLKEM_POLYVECBYTES, a_seed, MLKEM_SYMBYTES);
}

static void s_unpack_pk(dap_mlkem_polyvec *a_pk,
                         uint8_t a_seed[MLKEM_SYMBYTES],
                         const uint8_t a_packed[MLKEM_INDCPA_PUBLICKEYBYTES])
{
    MLKEM_NAMESPACE(_polyvec_frombytes)(a_pk, a_packed);
    memcpy(a_seed, a_packed + MLKEM_POLYVECBYTES, MLKEM_SYMBYTES);
}

static void s_pack_sk(uint8_t a_r[MLKEM_INDCPA_SECRETKEYBYTES], dap_mlkem_polyvec *a_sk)
{
    MLKEM_NAMESPACE(_polyvec_tobytes)(a_r, a_sk);
}

static void s_unpack_sk(dap_mlkem_polyvec *a_sk,
                         const uint8_t a_packed[MLKEM_INDCPA_SECRETKEYBYTES])
{
    MLKEM_NAMESPACE(_polyvec_frombytes)(a_sk, a_packed);
}

static void s_pack_ciphertext(uint8_t a_r[MLKEM_INDCPA_BYTES],
                               dap_mlkem_polyvec *a_b, dap_mlkem_poly *a_v)
{
    MLKEM_NAMESPACE(_polyvec_compress)(a_r, a_b);
    MLKEM_NAMESPACE(_poly_compress)(a_r + MLKEM_POLYVECCOMPRESSEDBYTES, a_v);
}

static void s_unpack_ciphertext(dap_mlkem_polyvec *a_b, dap_mlkem_poly *a_v,
                                 const uint8_t a_c[MLKEM_INDCPA_BYTES])
{
    MLKEM_NAMESPACE(_polyvec_decompress)(a_b, a_c);
    MLKEM_NAMESPACE(_poly_decompress)(a_v, a_c + MLKEM_POLYVECCOMPRESSEDBYTES);
}

/* ---- rejection sampling ---- */

static unsigned s_rej_uniform(int16_t *a_r, unsigned a_len,
                               const uint8_t *a_buf, unsigned a_buflen)
{
    unsigned l_ctr = 0, l_pos = 0;
    while (l_ctr < a_len && l_pos + 3 <= a_buflen) {
        uint16_t val0 = ((a_buf[l_pos] >> 0) | ((uint16_t)a_buf[l_pos + 1] << 8)) & 0xFFF;
        uint16_t val1 = ((a_buf[l_pos + 1] >> 4) | ((uint16_t)a_buf[l_pos + 2] << 4)) & 0xFFF;
        l_pos += 3;
        if (val0 < MLKEM_Q)
            a_r[l_ctr++] = (int16_t)val0;
        if (l_ctr < a_len && val1 < MLKEM_Q)
            a_r[l_ctr++] = (int16_t)val1;
    }
    return l_ctr;
}

/* ---- matrix generation ---- */

#define GEN_MATRIX_NBLOCKS ((12 * MLKEM_N / 8 * (1 << 12) / MLKEM_Q \
                             + MLKEM_XOF_BLOCKBYTES) / MLKEM_XOF_BLOCKBYTES)

static void s_gen_matrix(dap_mlkem_polyvec *a_mat, const uint8_t a_seed[MLKEM_SYMBYTES],
                          int a_transposed)
{
    uint8_t l_buf[GEN_MATRIX_NBLOCKS * MLKEM_XOF_BLOCKBYTES + 2];
    dap_mlkem_xof_state l_state;

    for (unsigned i = 0; i < MLKEM_K; i++) {
        for (unsigned j = 0; j < MLKEM_K; j++) {
            if (a_transposed)
                dap_mlkem_xof_absorb(&l_state, a_seed, (uint8_t)i, (uint8_t)j);
            else
                dap_mlkem_xof_absorb(&l_state, a_seed, (uint8_t)j, (uint8_t)i);

            dap_mlkem_xof_squeezeblocks(l_buf, GEN_MATRIX_NBLOCKS, &l_state);
            unsigned l_buflen = GEN_MATRIX_NBLOCKS * MLKEM_XOF_BLOCKBYTES;
            unsigned l_ctr = s_rej_uniform(a_mat[i].vec[j].coeffs, MLKEM_N, l_buf, l_buflen);

            while (l_ctr < MLKEM_N) {
                unsigned l_off = l_buflen % 3;
                for (unsigned k = 0; k < l_off; k++)
                    l_buf[k] = l_buf[l_buflen - l_off + k];
                dap_mlkem_xof_squeezeblocks(l_buf + l_off, 1, &l_state);
                l_buflen = l_off + MLKEM_XOF_BLOCKBYTES;
                l_ctr += s_rej_uniform(a_mat[i].vec[j].coeffs + l_ctr, MLKEM_N - l_ctr,
                                       l_buf, l_buflen);
            }
        }
    }
}

/* ---- IND-CPA ---- */

void MLKEM_NAMESPACE(_indcpa_keypair)(uint8_t a_pk[MLKEM_INDCPA_PUBLICKEYBYTES],
                                       uint8_t a_sk[MLKEM_INDCPA_SECRETKEYBYTES])
{
    uint8_t l_buf[2 * MLKEM_SYMBYTES];
    const uint8_t *l_publicseed = l_buf;
    const uint8_t *l_noiseseed  = l_buf + MLKEM_SYMBYTES;
    uint8_t l_nonce = 0;
    dap_mlkem_polyvec l_a[MLKEM_K], l_e, l_pkpv, l_skpv;

    dap_random_bytes(l_buf, MLKEM_SYMBYTES);
    dap_mlkem_hash_g(l_buf, l_buf, MLKEM_SYMBYTES);

    s_gen_matrix(l_a, l_publicseed, 0);

    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_poly_getnoise_eta1)(&l_skpv.vec[i], l_noiseseed, l_nonce++);
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_poly_getnoise_eta1)(&l_e.vec[i], l_noiseseed, l_nonce++);

    MLKEM_NAMESPACE(_polyvec_ntt)(&l_skpv);
    MLKEM_NAMESPACE(_polyvec_ntt)(&l_e);

    for (unsigned i = 0; i < MLKEM_K; i++) {
        MLKEM_NAMESPACE(_polyvec_pointwise_acc_montgomery)(&l_pkpv.vec[i], &l_a[i], &l_skpv);
        MLKEM_NAMESPACE(_poly_tomont)(&l_pkpv.vec[i]);
    }
    MLKEM_NAMESPACE(_polyvec_add)(&l_pkpv, &l_pkpv, &l_e);
    MLKEM_NAMESPACE(_polyvec_reduce)(&l_pkpv);

    s_pack_sk(a_sk, &l_skpv);
    s_pack_pk(a_pk, &l_pkpv, l_publicseed);
}

void MLKEM_NAMESPACE(_indcpa_enc)(uint8_t a_c[MLKEM_INDCPA_BYTES],
                                   const uint8_t a_m[MLKEM_INDCPA_MSGBYTES],
                                   const uint8_t a_pk[MLKEM_INDCPA_PUBLICKEYBYTES],
                                   const uint8_t a_coins[MLKEM_SYMBYTES])
{
    uint8_t l_seed[MLKEM_SYMBYTES];
    uint8_t l_nonce = 0;
    dap_mlkem_polyvec l_sp, l_pkpv, l_ep, l_at[MLKEM_K], l_bp;
    dap_mlkem_poly l_v, l_k, l_epp;

    s_unpack_pk(&l_pkpv, l_seed, a_pk);
    MLKEM_NAMESPACE(_poly_frommsg)(&l_k, a_m);
    s_gen_matrix(l_at, l_seed, 1);

    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_poly_getnoise_eta1)(l_sp.vec + i, a_coins, l_nonce++);
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_poly_getnoise_eta2)(l_ep.vec + i, a_coins, l_nonce++);
    MLKEM_NAMESPACE(_poly_getnoise_eta2)(&l_epp, a_coins, l_nonce++);

    MLKEM_NAMESPACE(_polyvec_ntt)(&l_sp);

    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_polyvec_pointwise_acc_montgomery)(&l_bp.vec[i], &l_at[i], &l_sp);
    MLKEM_NAMESPACE(_polyvec_pointwise_acc_montgomery)(&l_v, &l_pkpv, &l_sp);

    MLKEM_NAMESPACE(_polyvec_invntt_tomont)(&l_bp);
    MLKEM_NAMESPACE(_poly_invntt_tomont)(&l_v);

    MLKEM_NAMESPACE(_polyvec_add)(&l_bp, &l_bp, &l_ep);
    MLKEM_NAMESPACE(_poly_add)(&l_v, &l_v, &l_epp);
    MLKEM_NAMESPACE(_poly_add)(&l_v, &l_v, &l_k);
    MLKEM_NAMESPACE(_polyvec_reduce)(&l_bp);
    MLKEM_NAMESPACE(_poly_reduce)(&l_v);

    s_pack_ciphertext(a_c, &l_bp, &l_v);
}

void MLKEM_NAMESPACE(_indcpa_dec)(uint8_t a_m[MLKEM_INDCPA_MSGBYTES],
                                   const uint8_t a_c[MLKEM_INDCPA_BYTES],
                                   const uint8_t a_sk[MLKEM_INDCPA_SECRETKEYBYTES])
{
    dap_mlkem_polyvec l_bp, l_skpv;
    dap_mlkem_poly l_v, l_mp;

    s_unpack_ciphertext(&l_bp, &l_v, a_c);
    s_unpack_sk(&l_skpv, a_sk);

    MLKEM_NAMESPACE(_polyvec_ntt)(&l_bp);
    MLKEM_NAMESPACE(_polyvec_pointwise_acc_montgomery)(&l_mp, &l_skpv, &l_bp);
    MLKEM_NAMESPACE(_poly_invntt_tomont)(&l_mp);

    MLKEM_NAMESPACE(_poly_sub)(&l_mp, &l_v, &l_mp);
    MLKEM_NAMESPACE(_poly_reduce)(&l_mp);

    MLKEM_NAMESPACE(_poly_tomsg)(a_m, &l_mp);
}
