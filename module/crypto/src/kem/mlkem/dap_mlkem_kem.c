/**
 * @file dap_mlkem_kem.c
 * @brief CCA-secure KEM for ML-KEM (FIPS 203).
 *
 * Implements key generation, encapsulation, and decapsulation with
 * implicit rejection (FO transform).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "dap_mlkem_kem.h"
#include "dap_mlkem_ctx.h"
#include "dap_mlkem_indcpa.h"
#include "dap_mlkem_verify.h"
#include "dap_mlkem_symmetric.h"
#include "dap_rand.h"

int MLKEM_NAMESPACE(_kem_keypair)(uint8_t *a_pk, uint8_t *a_sk)
{
    MLKEM_NAMESPACE(_indcpa_keypair)(a_pk, a_sk);
    memcpy(a_sk + MLKEM_INDCPA_SECRETKEYBYTES, a_pk, MLKEM_INDCPA_PUBLICKEYBYTES);
    dap_mlkem_hash_h(a_sk + MLKEM_SECRETKEYBYTES - 2 * MLKEM_SYMBYTES, a_pk, MLKEM_PUBLICKEYBYTES);
    dap_random_bytes(a_sk + MLKEM_SECRETKEYBYTES - MLKEM_SYMBYTES, MLKEM_SYMBYTES);
    return 0;
}

int MLKEM_NAMESPACE(_kem_enc)(uint8_t *a_ct, uint8_t *a_ss, const uint8_t *a_pk)
{
    uint8_t l_buf[2 * MLKEM_SYMBYTES];
    uint8_t l_kr[2 * MLKEM_SYMBYTES];

    dap_random_bytes(l_buf, MLKEM_SYMBYTES);
    dap_mlkem_hash_h(l_buf, l_buf, MLKEM_SYMBYTES);

    dap_mlkem_hash_h(l_buf + MLKEM_SYMBYTES, a_pk, MLKEM_PUBLICKEYBYTES);
    dap_mlkem_hash_g(l_kr, l_buf, 2 * MLKEM_SYMBYTES);

    MLKEM_NAMESPACE(_indcpa_enc)(a_ct, l_buf, a_pk, l_kr + MLKEM_SYMBYTES);

    dap_mlkem_hash_h(l_kr + MLKEM_SYMBYTES, a_ct, MLKEM_CIPHERTEXTBYTES);
    dap_mlkem_kdf(a_ss, l_kr, 2 * MLKEM_SYMBYTES);
    return 0;
}

int MLKEM_NAMESPACE(_kem_dec)(uint8_t *a_ss, const uint8_t *a_ct, const uint8_t *a_sk)
{
    uint8_t l_buf[2 * MLKEM_SYMBYTES];
    uint8_t l_kr[2 * MLKEM_SYMBYTES];
    uint8_t l_cmp[MLKEM_CIPHERTEXTBYTES];
    const uint8_t *l_pk = a_sk + MLKEM_INDCPA_SECRETKEYBYTES;

    MLKEM_NAMESPACE(_indcpa_dec)(l_buf, a_ct, a_sk);

    memcpy(l_buf + MLKEM_SYMBYTES,
           a_sk + MLKEM_SECRETKEYBYTES - 2 * MLKEM_SYMBYTES,
           MLKEM_SYMBYTES);
    dap_mlkem_hash_g(l_kr, l_buf, 2 * MLKEM_SYMBYTES);

    MLKEM_NAMESPACE(_indcpa_enc)(l_cmp, l_buf, l_pk, l_kr + MLKEM_SYMBYTES);

    int l_fail = dap_mlkem_verify(a_ct, l_cmp, MLKEM_CIPHERTEXTBYTES);

    dap_mlkem_hash_h(l_kr + MLKEM_SYMBYTES, a_ct, MLKEM_CIPHERTEXTBYTES);
    dap_mlkem_cmov(l_kr, a_sk + MLKEM_SECRETKEYBYTES - MLKEM_SYMBYTES,
                   MLKEM_SYMBYTES, (uint8_t)l_fail);
    dap_mlkem_kdf(a_ss, l_kr, 2 * MLKEM_SYMBYTES);
    return 0;
}

/* =========================================================================
 * Context-based API — persistent NTT state
 * ========================================================================= */

int MLKEM_NAMESPACE(_ctx_init)(dap_mlkem_ctx_t *a_ctx,
                                const uint8_t *a_pk,
                                const uint8_t *a_sk)
{
    MLKEM_NAMESPACE(_indcpa_ctx_init_pk)(a_ctx, a_pk);
    MLKEM_NAMESPACE(_indcpa_ctx_init_sk)(a_ctx, a_sk);
    dap_mlkem_hash_h(a_ctx->h_pk, a_pk, MLKEM_PUBLICKEYBYTES);
    memcpy(a_ctx->z, a_sk + MLKEM_SECRETKEYBYTES - MLKEM_SYMBYTES, MLKEM_SYMBYTES);
    a_ctx->flags = DAP_MLKEM_CTX_HAS_PK | DAP_MLKEM_CTX_HAS_SK;
    return 0;
}

int MLKEM_NAMESPACE(_ctx_init_enc)(dap_mlkem_ctx_t *a_ctx,
                                    const uint8_t *a_pk)
{
    MLKEM_NAMESPACE(_indcpa_ctx_init_pk)(a_ctx, a_pk);
    dap_mlkem_hash_h(a_ctx->h_pk, a_pk, MLKEM_PUBLICKEYBYTES);
    a_ctx->flags = DAP_MLKEM_CTX_HAS_PK;
    return 0;
}

int MLKEM_NAMESPACE(_kem_keypair_ctx)(dap_mlkem_ctx_t *a_ctx,
                                       uint8_t *a_pk, uint8_t *a_sk)
{
    MLKEM_NAMESPACE(_kem_keypair)(a_pk, a_sk);
    return MLKEM_NAMESPACE(_ctx_init)(a_ctx, a_pk, a_sk);
}

int MLKEM_NAMESPACE(_kem_enc_ctx)(uint8_t *a_ct, uint8_t *a_ss,
                                   const dap_mlkem_ctx_t *a_ctx)
{
    uint8_t l_buf[2 * MLKEM_SYMBYTES];
    uint8_t l_kr[2 * MLKEM_SYMBYTES];

    dap_random_bytes(l_buf, MLKEM_SYMBYTES);
    dap_mlkem_hash_h(l_buf, l_buf, MLKEM_SYMBYTES);

    memcpy(l_buf + MLKEM_SYMBYTES, a_ctx->h_pk, MLKEM_SYMBYTES);
    dap_mlkem_hash_g(l_kr, l_buf, 2 * MLKEM_SYMBYTES);

    MLKEM_NAMESPACE(_indcpa_enc_ctx)(a_ct, l_buf, a_ctx, l_kr + MLKEM_SYMBYTES);

    dap_mlkem_hash_h(l_kr + MLKEM_SYMBYTES, a_ct, MLKEM_CIPHERTEXTBYTES);
    dap_mlkem_kdf(a_ss, l_kr, 2 * MLKEM_SYMBYTES);
    return 0;
}

int MLKEM_NAMESPACE(_kem_dec_ctx)(uint8_t *a_ss, const uint8_t *a_ct,
                                   const dap_mlkem_ctx_t *a_ctx)
{
    uint8_t l_buf[2 * MLKEM_SYMBYTES];
    uint8_t l_kr[2 * MLKEM_SYMBYTES];
    uint8_t l_cmp[MLKEM_CIPHERTEXTBYTES];

    MLKEM_NAMESPACE(_indcpa_dec_ctx)(l_buf, a_ct, a_ctx);

    memcpy(l_buf + MLKEM_SYMBYTES, a_ctx->h_pk, MLKEM_SYMBYTES);
    dap_mlkem_hash_g(l_kr, l_buf, 2 * MLKEM_SYMBYTES);

    MLKEM_NAMESPACE(_indcpa_enc_ctx)(l_cmp, l_buf, a_ctx, l_kr + MLKEM_SYMBYTES);

    int l_fail = dap_mlkem_verify(a_ct, l_cmp, MLKEM_CIPHERTEXTBYTES);

    dap_mlkem_hash_h(l_kr + MLKEM_SYMBYTES, a_ct, MLKEM_CIPHERTEXTBYTES);
    dap_mlkem_cmov(l_kr, a_ctx->z, MLKEM_SYMBYTES, (uint8_t)l_fail);
    dap_mlkem_kdf(a_ss, l_kr, 2 * MLKEM_SYMBYTES);
    return 0;
}
