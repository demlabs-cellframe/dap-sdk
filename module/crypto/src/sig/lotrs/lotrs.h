/*
 * LoTRS — threshold ring signature scheme.
 *
 * keygen, kagg, sign (2-round), verify.
 */

#pragma once
#ifndef _LOTRS_H_
#define _LOTRS_H_

#include <stdint.h>
#include <stddef.h>

#include "lotrs_params.h"
#include "lotrs_ring.h"

/* Public key: k polynomials in R_q. */
typedef struct lotrs_pk {
    lotrs_polyvec_t a_hat;  /* k polynomials */
} lotrs_pk_t;

/* Secret key: (l + k) short polynomials. */
typedef struct lotrs_sk {
    lotrs_polyvec_t s;      /* l + k polynomials, each in {-eta..eta} */
} lotrs_sk_t;

/* Key pair. */
typedef struct lotrs_keypair {
    lotrs_pk_t pk;
    lotrs_sk_t sk;
} lotrs_keypair_t;

/* Ring public key table: N columns × T rows. */
typedef struct lotrs_ring_pk {
    lotrs_pk_t *pks;        /* N * T public keys, row-major */
    uint32_t    N;
    uint32_t    T;
} lotrs_ring_pk_t;

/* Signature. */
typedef struct lotrs_signature {
    uint8_t *data;          /* serialized signature */
    size_t   len;
} lotrs_signature_t;

/* --- Key generation --- */
int lotrs_keygen(lotrs_keypair_t *kp, const lotrs_params_t *par,
                 const uint8_t seed[32]);

/* --- Ring PK aggregation (KAgg) --- */
int lotrs_kagg(lotrs_polyvec_t *agg_keys, const lotrs_ring_pk_t *ring,
               const lotrs_params_t *par);

/* --- Sign (non-interactive, single-signer model) --- */
int lotrs_sign(lotrs_signature_t *sig,
               const lotrs_params_t *par,
               const lotrs_ring_pk_t *ring,
               const lotrs_sk_t *sk,
               uint32_t signer_idx,
               const uint8_t *msg, size_t msg_len,
               const uint8_t seed[32]);

/* --- Verify --- */
int lotrs_verify(const lotrs_signature_t *sig,
                 const lotrs_params_t *par,
                 const lotrs_ring_pk_t *ring,
                 const uint8_t *msg, size_t msg_len);

/* --- Cleanup --- */
void lotrs_pk_free(lotrs_pk_t *pk);
void lotrs_sk_free(lotrs_sk_t *sk);
void lotrs_ring_pk_free(lotrs_ring_pk_t *ring);
void lotrs_signature_free(lotrs_signature_t *sig);

#endif /* _LOTRS_H_ */
