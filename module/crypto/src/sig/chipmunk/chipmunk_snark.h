/*
 * chipmunk_snark.h — Lattice-based SNARK (Ligero-style) for ring membership proofs.
 *
 * Post-quantum succinct non-interactive argument of knowledge based on:
 * - Hash-based polynomial commitments over R_q^{(e)} (degree-6 extension)
 * - Challenges from subtractive set S = F_{q^6} \ {0}  (|S| = q^6 - 1 ~ 2^129.6)
 * - Ring membership circuit as polynomial constraints over R_q
 * - QROM Fiat-Shamir transform
 *
 * All operations over R_q^{(e)} = R_q[Y]/(Phi_9(Y)) where Phi_9 = Y^6+Y^3+1.
 * R_q = Z_q[X]/(X^512+1), q = 3168257 (prime).
 *
 * Soundness model (Phase 1 rewrite):
 * - Alpha challenge drawn from F_{q^6}\{0}: evaluation soundness ~129 bits per check
 * - Constraint polynomial verified via quotient relation at EXT_ALPHA_CHECKS
 *   random F_q points: combined soundness >> 128 bits
 * - w_commit binding: constraint polynomial reconstructed from public inputs,
 *   mismatch with z_commit causes rejection
 *
 * NOTE: Phase 1 uses full-polynomial opening proofs (z, q sent in clear).
 *       Phase 2+ will replace with Merkle-based polynomial commitment scheme
 *       for true succinctness (~200-400 byte proofs).
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
#define CHIPMUNK_SNARK_EXT_DEG      6       /* Extension degree e=6 (Phi_9) */

/* Polynomial commitment */
#define CHIPMUNK_SNARK_COMMIT_BYTES 32      /* SHA3-256 hash of coefficients */

/* Number of random F_q points for quotient relation verification.
 * Each check: z(beta) == q(beta) * (beta - alpha_scalar) where alpha_scalar
 * is the Y^0 component of the extension challenge.
 * Soundness per check: ~2*Q^{-1} ~ 2^{-21.6} (two evaluation points).
 * With 11 checks: 11 * 21.6 ~ 238 bits >> 128 bits.
 * The extension alpha provides the primary ~129-bit soundness bound. */
#define CHIPMUNK_SNARK_QUOTIENT_CHECKS  11

/* Opening proof: z and q polynomials sent in full (interim Phase 1).
 * Each polynomial = CHIPMUNK_N * sizeof(int32_t) = 512 * 4 = 2048 bytes.
 * Total opening: 4096 bytes. Phase 2+ will replace with Merkle proofs. */
#define CHIPMUNK_SNARK_OPENING_POLYS    2   /* z + q */
#define CHIPMUNK_SNARK_OPENING_BYTES    \
    (CHIPMUNK_SNARK_OPENING_POLYS * CHIPMUNK_N * (int)sizeof(int32_t))

/* Total proof size estimate (Phase 5: alpha removed, ~4.4 KB) */
#define CHIPMUNK_SNARK_PROOF_MAX    (CHIPMUNK_SNARK_OPENING_BYTES + 256)

/* Subtractive set size: |S| = q^6 - 1 ~ 2^{129.6} */
/* Per-round soundness: 2/|S| ~ 2^{-128.6} */

/* -------------------------------------------------------------------------
 * Types
 * ---------------------------------------------------------------------- */

/* Polynomial commitment: SHA3-256 hash of serialized coefficients */
typedef struct chipmunk_snark_commit {
    uint8_t hash[CHIPMUNK_SNARK_COMMIT_BYTES];
} chipmunk_snark_commit_t;

/* Ring membership statement */
typedef struct chipmunk_snark_statement {
    const chipmunk_lrs_public_key_t *ring;      /* Ring of public keys */
    size_t ring_size;                           /* N */
    const uint8_t *message;                     /* Message being signed */
    size_t message_size;
    chipmunk_snark_commit_t ring_commit;        /* Commitment to ring hash */
} chipmunk_snark_statement_t;

/* Ring membership witness (private) */
typedef struct chipmunk_snark_witness {
    uint32_t signer_index;                      /* Which key signed */
    chipmunk_poly_t secret_key[CHIPMUNK_LRS_K * 2]; /* Secret witness (s0[GAMMA]+s1[GAMMA]) */
    chipmunk_poly_t indicator;                  /* b in {0,1}^N */
} chipmunk_snark_witness_t;

/* Full SNARK proof (Phase 1 interim format) */
typedef struct chipmunk_snark_proof {
    /* Commitment phase */
    chipmunk_snark_commit_t w_commit;           /* Commitment to witness polynomial */
    chipmunk_snark_commit_t z_commit;           /* Commitment to constraint polynomial */
    chipmunk_snark_commit_t q_commit;           /* Commitment to quotient polynomial */
    chipmunk_snark_commit_t r_commit;           /* Commitment to randomizer (F_q^6) */

    /* Phase 5: alpha removed from proof struct.
     * The verifier re-derives alpha from the QROM transcript, so storing
     * it in the proof was redundant (12 KB of wasted space).
     * Previous proof size: ~16.5 KB.  After removal: ~4.5 KB. */

    /* Opening proof: serialized z and q polynomials.
     * b (indicator) is NOT included to protect signer privacy.
     * Verifier reconstructs z, q and checks commitments + quotient relation. */
    uint8_t opening_proof[CHIPMUNK_SNARK_OPENING_BYTES];
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
 * revealing j.
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
