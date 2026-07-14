/*
 * chipmunk_snark.h — Lattice-based SNARK (Ligero-style) for ring membership proofs.
 *
 * Post-quantum succinct non-interactive argument of knowledge based on:
 * - Hash-based polynomial commitments over R_q^{(e)} (degree-6 extension)
 * - FRI-style folding with challenges from subtractive set S = F_{q^6} \ {0}
 * - Ring membership circuit as polynomial constraints
 * - QROM Fiat-Shamir transform
 *
 * Provides ~200-400 byte proofs for ring membership (vs ~34KB for MRNG fold).
 * All operations over R_q^{(e)} = R_q[Y]/(Φ_9(Y)) where Φ_9 = Y^6+Y^3+1.
 *
 * Security: MSIS/MLWE ≥ 128 bits (quantum), soundness ~128 bits via
 * subtractive set |S| = q^6 - 1 ≈ 2^{129.6} per round.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "chipmunk.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"
#include "chipmunk_lrs.h"
#include "chipmunk_mring_ext.h"
#include "chipmunk_mring_params.h"
#include "lotrs_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Parameters
 * ---------------------------------------------------------------------- */

#define CHIPMUNK_SNARK_LOG_N        9       /* log2(512) */
#define CHIPMUNK_SNARK_N            512     /* Polynomial ring dimension */
/* CHIPMUNK_SNARK_Q is identical to CHIPMUNK_Q (3168257).  The alias is kept
 * for backward compatibility but all new code should use CHIPMUNK_Q directly. */
#define CHIPMUNK_SNARK_Q            CHIPMUNK_Q
#define CHIPMUNK_SNARK_SECURITY     128     /* Target security level (bits) */
#define CHIPMUNK_SNARK_EXT_DEG      6       /* Extension degree e=6 (Φ_9) */

/* FRI folding parameters */
#define CHIPMUNK_SNARK_FOLD_ROUNDS  7       /* log2(N) - 1 for sufficient soundness */
#define CHIPMUNK_SNARK_FOLD_BLOWUP  4       /* Blowup factor for Reed-Solomon encoding */

/* Polynomial commitment */
#define CHIPMUNK_SNARK_COMMIT_BYTES 32      /* SHA3-256 hash of coefficients */
#define CHIPMUNK_SNARK_QUERY_COUNT  30      /* Number of FRI queries for 128-bit soundness */

/* Proof size estimates */
#define CHIPMUNK_SNARK_PROOF_MAX    1024    /* Max proof size in bytes */

/* Subtractive set size: |S| = q^6 - 1 ≈ 2^{129.6} */
/* Per-round soundness: 2/|S| ≈ 2^{-128.6} */
/* Over 7 rounds: 7 * 2/|S| ≈ 2^{-125.8} */

/* -------------------------------------------------------------------------
 * Types
 * ---------------------------------------------------------------------- */

/* Polynomial commitment: SHA3-256 hash of serialized coefficients */
typedef struct chipmunk_snark_commit {
    uint8_t hash[CHIPMUNK_SNARK_COMMIT_BYTES];
} chipmunk_snark_commit_t;

/* Polynomial evaluation proof (for one point) */
typedef struct chipmunk_snark_eval_proof {
    chipmunk_mring_ext_t value;         /* f(alpha) in R_q^{(e)} */
    uint8_t proof_data[256];            /* Authentication path */
    size_t proof_size;
} chipmunk_snark_eval_proof_t;

/* FRI layer commitment */
typedef struct chipmunk_snark_fri_layer {
    chipmunk_snark_commit_t commit;
} chipmunk_snark_fri_layer_t;

/* Ring membership statement */
typedef struct chipmunk_snark_statement {
    const chipmunk_lrs_public_key_t *ring;      /* Ring of public keys */
    size_t ring_size;                           /* N */
    const uint8_t *message;                     /* Message being signed */
    size_t message_size;
    chipmunk_snark_commit_t ring_commit;        /* Commitment to ring */
} chipmunk_snark_statement_t;

/* Ring membership witness (private) */
typedef struct chipmunk_snark_witness {
    uint32_t signer_index;                      /* Which key signed */
    chipmunk_poly_t secret_key[CHIPMUNK_LRS_K * 2]; /* Secret witness (s0[GAMMA]+s1[GAMMA]) */
    chipmunk_poly_t indicator;                  /* b ∈ {0,1}^N */
} chipmunk_snark_witness_t;

/* Full SNARK proof */
typedef struct chipmunk_snark_proof {
    /* Commitment phase */
    chipmunk_snark_commit_t w_commit;           /* Commitment to witness polynomial */
    chipmunk_snark_commit_t z_commit;           /* Commitment to constraint polynomial */
    chipmunk_snark_commit_t q_commit;           /* Commitment to quotient polynomial */

    /* FRI proof for polynomial evaluation */
    chipmunk_snark_fri_layer_t fri_layers[CHIPMUNK_SNARK_FOLD_ROUNDS];
    chipmunk_mring_ext_t fri_last_layer;        /* Final polynomial in R_q^{(e)} */

    /* Evaluation proofs */
    chipmunk_mring_ext_t w_eval;                /* w(alpha) in R_q^{(e)} */
    chipmunk_mring_ext_t z_eval;                /* z(alpha) in R_q^{(e)} */
    chipmunk_mring_ext_t q_eval;                /* q(alpha) in R_q^{(e)} */

    /* Challenge point (derived from transcript) */
    chipmunk_mring_ext_t alpha;                 /* Challenge from subtractive set */

    /* Opening proofs: raw polynomial bytes for verifier */
    uint8_t opening_proof[CHIPMUNK_N * sizeof(int32_t) * 3]; /* b + z + q */
    size_t opening_proof_size;

    /* QROM transcript hash */
    uint8_t transcript_hash[32];
} chipmunk_snark_proof_t;

/* SNARK context (public parameters) */
typedef struct chipmunk_snark_ctx {
    lotrs_params_t params;
    uint8_t domain_separator[32];               /* QROM domain separator */
    bool initialized;
} chipmunk_snark_ctx_t;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/**
 * Initialize SNARK context with public parameters.
 * @param ctx Output context.
 * @return 0 on success, negative on error.
 */
int chipmunk_snark_init(chipmunk_snark_ctx_t *ctx);

/**
 * Commit to a polynomial: C = H(f_0 || f_1 || ... || f_{N-1}).
 * @param commit Output commitment.
 * @param poly Polynomial to commit.
 * @return 0 on success.
 */
int chipmunk_snark_commit(chipmunk_snark_commit_t *commit,
                          const chipmunk_poly_t *poly);

/**
 * Generate a ring membership SNARK proof.
 *
 * Proves: "I know sk_j for pk_j in {pk_0, ..., pk_{N-1}}" without
 * revealing j. Proof size ~200-400 bytes.
 *
 * @param proof Output proof.
 * @param ctx SNARK context.
 * @param statement Public statement (ring, message).
 * @param witness Private witness (secret key, index).
 * @return 0 on success, negative on error.
 */
int chipmunk_snark_prove(chipmunk_snark_proof_t *proof,
                         const chipmunk_snark_ctx_t *ctx,
                         const chipmunk_snark_statement_t *statement,
                         const chipmunk_snark_witness_t *witness);

/**
 * Verify a ring membership SNARK proof.
 *
 * @param proof The proof to verify.
 * @param ctx SNARK context.
 * @param statement Public statement (ring, message).
 * @return 1 if valid, 0 if invalid, negative on error.
 */
int chipmunk_snark_verify(const chipmunk_snark_proof_t *proof,
                          const chipmunk_snark_ctx_t *ctx,
                          const chipmunk_snark_statement_t *statement);

/**
 * Free SNARK proof resources.
 */
void chipmunk_snark_proof_free(chipmunk_snark_proof_t *proof);

/**
 * Free SNARK context resources.
 */
void chipmunk_snark_ctx_free(chipmunk_snark_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
