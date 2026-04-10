/**
 * @file dap_mlkem_ctx.h
 * @brief Persistent context for ML-KEM — pre-computed NTT state.
 *
 * Caches matrix A^T, secret/public keys in NTT domain, and mulcache for sk.
 * Eliminates per-call XOF matrix regeneration and key deserialization.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include "dap_mlkem_params.h"

#define DAP_MLKEM_CTX_HAS_SK  0x01u
#define DAP_MLKEM_CTX_HAS_PK  0x02u

typedef struct dap_mlkem_ctx {
    dap_mlkem_polyvec mat_t[MLKEM_K];
    dap_mlkem_polyvec sk_ntt;
    dap_mlkem_polyvec_mulcache sk_cache;
    dap_mlkem_polyvec pk_ntt;
    uint8_t h_pk[MLKEM_SYMBYTES];
    uint8_t z[MLKEM_SYMBYTES];
    uint8_t seed[MLKEM_SYMBYTES];
    uint32_t flags;
} dap_mlkem_ctx_t;

int MLKEM_NAMESPACE(_ctx_init)(dap_mlkem_ctx_t *a_ctx,
                                const uint8_t *a_pk,
                                const uint8_t *a_sk);

int MLKEM_NAMESPACE(_ctx_init_enc)(dap_mlkem_ctx_t *a_ctx,
                                    const uint8_t *a_pk);

int MLKEM_NAMESPACE(_kem_keypair_ctx)(dap_mlkem_ctx_t *a_ctx,
                                       uint8_t *a_pk, uint8_t *a_sk);

int MLKEM_NAMESPACE(_kem_enc_ctx)(uint8_t *a_ct, uint8_t *a_ss,
                                   const dap_mlkem_ctx_t *a_ctx);

int MLKEM_NAMESPACE(_kem_dec_ctx)(uint8_t *a_ss, const uint8_t *a_ct,
                                   const dap_mlkem_ctx_t *a_ctx);
