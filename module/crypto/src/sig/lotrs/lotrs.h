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

/* --- Multi-round threshold signing (M9.4) --- */

/* Round 1 state (per signer). */
typedef struct lotrs_round1_state {
    lotrs_polyvec_t *y;   /* random masks, l+k polys */
    lotrs_polyvec_t *w;   /* commitment, k polys */
    uint8_t rho[32];      /* randomness seed */
    uint32_t signer_idx;
} lotrs_round1_state_t;

/* Round 1 output (sent to combiner). */
typedef struct lotrs_round1_output {
    lotrs_polyvec_t *w;   /* commitment, k polys */
} lotrs_round1_output_t;

/* Round 2 output (per signer, sent to combiner). */
typedef struct lotrs_round2_output {
    lotrs_polyvec_t *z_u;   /* response share, l+k polys */
} lotrs_round2_output_t;

/*
 * Round 1: each signer generates random masks and commitment.
 *
 * @param a_state  Receives signer's local state (secret).
 * @param a_out    Receives output to broadcast.
 * @param a_par    Parameters.
 * @param a_sk     Signer's secret key.
 * @param a_idx    Signer's index in the ring.
 * @param a_seed   Randomness seed (32 bytes).
 *
 * @return 0 on success.
 */
int lotrs_sign_round1(lotrs_round1_state_t *a_state,
                      lotrs_round1_output_t *a_out,
                      const lotrs_params_t *a_par,
                      const lotrs_sk_t *a_sk,
                      uint32_t a_idx,
                      const uint8_t a_seed[32]);

/*
 * Round 2: each signer computes response share.
 *
 * @param a_out    Receives response to send to combiner.
 * @param a_state  Signer's local state from round 1.
 * @param a_par    Parameters.
 * @param a_sk     Signer's secret key.
 * @param a_w_agg  Aggregated commitment (sum of all w_u).
 * @param a_msg    Message.
 * @param a_msg_len Message length.
 *
 * @return 0 on success, -EAGAIN if rejection (retry round 1).
 */
int lotrs_sign_round2(lotrs_round2_output_t *a_out,
                      const lotrs_round1_state_t *a_state,
                      const lotrs_params_t *a_par,
                      const lotrs_sk_t *a_sk,
                      const lotrs_polyvec_t *a_w_agg,
                      const uint8_t *a_msg, size_t a_msg_len);

/*
 * Aggregate T round-2 outputs into final signature.
 *
 * @param a_sig    Receives final signature.
 * @param a_par    Parameters.
 * @param a_r1_outs Array of T round-1 outputs (for proof consistency check).
 * @param a_r2_outs Array of T round-2 outputs.
 * @param a_T      Number of signers (threshold).
 *
 * @return 0 on success.
 */
int lotrs_sign_aggregate(lotrs_signature_t *a_sig,
                         const lotrs_params_t *a_par,
                         const lotrs_round1_output_t *a_r1_outs,
                         const lotrs_round2_output_t *a_r2_outs,
                         uint32_t a_T);

/* Cleanup. */
void lotrs_round1_state_free(lotrs_round1_state_t *a_state);
void lotrs_round1_output_free(lotrs_round1_output_t *a_out);
void lotrs_round2_output_free(lotrs_round2_output_t *a_out);

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
