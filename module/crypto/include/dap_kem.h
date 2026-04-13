/**
 * @file dap_kem.h
 * @brief Generic KEM API — external interface for ALL KEM algorithms.
 *
 * Analogous to dap_sign for digital signatures.
 * Dispatches to algorithm-specific implementations (ML-KEM, NTRU Prime, NewHope).
 *
 * Context API pre-computes NTT state / matrix for repeated encaps/decaps
 * with the same keypair — available for algorithms that support it (ML-KEM).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "dap_enc_key.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DAP_KEM_ALG_ML_KEM_512 = 0,
    DAP_KEM_ALG_ML_KEM_768,
    DAP_KEM_ALG_ML_KEM_1024,
    DAP_KEM_ALG_NTRU_PRIME,
    DAP_KEM_ALG_NEWHOPE,
    DAP_KEM_ALG_COUNT
} dap_kem_alg_t;

typedef struct dap_kem_ctx {
    dap_kem_alg_t alg;
    void *state;
} dap_kem_ctx_t;

/* ---- Algorithm metadata ---- */

const char *dap_kem_alg_name(dap_kem_alg_t a_alg);
bool        dap_kem_alg_is_deprecated(dap_kem_alg_t a_alg);
bool        dap_kem_alg_has_ctx(dap_kem_alg_t a_alg);

size_t dap_kem_publickey_size(dap_kem_alg_t a_alg);
size_t dap_kem_secretkey_size(dap_kem_alg_t a_alg);
size_t dap_kem_ciphertext_size(dap_kem_alg_t a_alg);
size_t dap_kem_sharedsecret_size(dap_kem_alg_t a_alg);

/* ---- Type conversions (like dap_sign_type_from_key_type) ---- */

dap_kem_alg_t      dap_kem_alg_from_enc_key_type(dap_enc_key_type_t a_key_type);
dap_enc_key_type_t dap_kem_alg_to_enc_key_type(dap_kem_alg_t a_alg);

dap_kem_alg_t dap_kem_alg_from_str(const char *a_str);
const char   *dap_kem_alg_to_str(dap_kem_alg_t a_alg);

/* ---- Stateless KEM operations ---- */

int dap_kem_keypair(dap_kem_alg_t a_alg, uint8_t *a_pk, uint8_t *a_sk);
int dap_kem_encaps(dap_kem_alg_t a_alg, uint8_t *a_ct, uint8_t *a_ss,
                   const uint8_t *a_pk);
int dap_kem_decaps(dap_kem_alg_t a_alg, uint8_t *a_ss, const uint8_t *a_ct,
                   const uint8_t *a_sk);

/* ---- Persistent context (pre-computed NTT state; ML-KEM only) ---- */

int  dap_kem_ctx_create(dap_kem_ctx_t *a_ctx, dap_kem_alg_t a_alg,
                        const uint8_t *a_pk, const uint8_t *a_sk);
int  dap_kem_ctx_create_enc(dap_kem_ctx_t *a_ctx, dap_kem_alg_t a_alg,
                            const uint8_t *a_pk);
int  dap_kem_ctx_encaps(uint8_t *a_ct, uint8_t *a_ss,
                        const dap_kem_ctx_t *a_ctx);
int  dap_kem_ctx_decaps(uint8_t *a_ss, const uint8_t *a_ct,
                        const dap_kem_ctx_t *a_ctx);
void dap_kem_ctx_destroy(dap_kem_ctx_t *a_ctx);

#ifdef __cplusplus
}
#endif
