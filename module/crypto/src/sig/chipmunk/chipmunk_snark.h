/*
 * chipmunk_snark.h — Lattice-based SNARK (Ligero-style) for ring membership proofs.
 *
 * Post-quantum succinct non-interactive argument of knowledge based on:
 * - Hash-based polynomial commitments over R_q^{(e)} (degree-6 extension)
 * - Challenges from subtractive set S = F_{q^6} \ {0}  (|S| = q^6 - 1 ~ 2^129.6)
 * - Ring membership circuit as polynomial constraints over R_q
 * - QROM Fiat-Shamir transform
 * - FRI-DEEP polynomial commitment scheme (Phase 9.11)
 *
 * All operations over R_q^{(e)} = R_q[Y]/(Phi_9(Y)) where Phi_9 = Y^6+Y^3+1.
 * R_q = Z_q[X]/(X^512+1), q = 3168257 (prime).
 *
 * Soundness model (Phase 9.11 — FRI-PCS integration):
 * - Alpha challenge from F_{q^6}\{0}: extension soundness ~129 bits
 * - Quotient checks at 11 random F_q points: ~238 bits
 * - FRI proximity (8 queries): ~8 bits
 * - Grinding PoW: ~16 bits
 * - Combined: ~391 bits >> 128-bit post-quantum target
 *
 * Proof format:
 *   V1 (legacy): raw z+q polynomials in opening_proof[4096]
 *   V2 (FRI-PCS): FRI proof of q(X) + raw polynomials for algebraic checks
 *                 (bridge phase — DEEP elimination of raw polys is Phase 9.12+)
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
#include "chipmunk_fri.h"
#include "chipmunk_fri_transcript.h"

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

/* Proof format versioning. */
#define CHIPMUNK_SNARK_PROOF_VERSION_V1   0u   /* Raw z+q polynomials (legacy) */
#define CHIPMUNK_SNARK_PROOF_VERSION_V2   1u   /* FRI-PCS commitment + raw polys (bridge) */

/* FRI domain separator for SNARK integration (exactly 16 bytes). */
#define CHIPMUNK_SNARK_FRI_DOMAIN    "CHIPMUNK-SNARK-FRI"

/* Total proof size estimate: large enough for V1 or V2 struct.
 * V2 struct includes opening_proof (4096) + fri_proof (2784) + other fields. */
#define CHIPMUNK_SNARK_PROOF_MAX    (sizeof(chipmunk_snark_proof_t))

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

/* Full SNARK proof (supports V1 legacy and V2 FRI-PCS formats). */
typedef struct chipmunk_snark_proof {
    /* Proof format version: 0 = V1 (raw poly), 1 = V2 (FRI bridge) */
    uint8_t proof_version;

    /* Commitment phase */
    chipmunk_snark_commit_t w_commit;           /* Commitment to witness polynomial */
    chipmunk_snark_commit_t z_commit;           /* Commitment to constraint polynomial */
    chipmunk_snark_commit_t q_commit;           /* Commitment to quotient polynomial */
    chipmunk_snark_commit_t r_commit;           /* Commitment to randomizer (F_q^6) */

    /* Opening proof: serialized z and q polynomials.
     * In V1: used for algebraic verification checks.
     * In V2: retained for algebraic checks while FRI proof provides binding.
     * b (indicator) is NOT included to protect signer privacy. */
    uint8_t opening_proof[CHIPMUNK_SNARK_OPENING_BYTES];
    size_t opening_proof_size;

    /* V2 FRI proof fields (populated when proof_version == V2).
     * FRI proof commits to q(X) with Fiat-Shamir transcript binding. */
    chipmunk_fri_proof_t   fri_proof;           /* FRI commit + query openings */
    uint32_t               fri_grinding_nonce;  /* Grinding PoW nonce */

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
