/*
 * chipmunk_stark.h — Lattice-based STARK (Ligero-style) for ring membership proofs.
 *
 * Post-quantum succinct non-interactive argument of knowledge based on:
 * - Hash-based polynomial commitments over R_q^{(e)} (degree-6 extension)
 * - Challenges from subtractive set S = F_{q^6} \ {0}
 * - Ring membership circuit as polynomial constraints over R_q
 * - QROM Fiat-Shamir transform
 * - FRI-DEEP polynomial commitment scheme (Phase 9.11)
 *
 * All operations over R_q^{(e)} = R_q[Y]/(Phi_9(Y)) where Phi_9 = Y^6+Y^3+1.
 * R_q = Z_q[X]/(X^d+1), q prime with q mod 9 ∈ {2,5}.
 *
 * Phase 9.13: Universal parameterization via chipmunk_stark_params_t.
 * Supports arbitrary (d, q) with NTT-compatible q. Predefined sets:
 *   LRS:  d=512, q=3168257   (129-bit extension security)
 *   Ring: d=128, q=4206593   (132-bit extension security)
 *   Test: d=32,  q=4206593   (fast unit tests)
 *
 * Soundness: ~391 bits (129 ext + 238 quotient + 8 FRI + 16 grinding).
 * Proof format: FRI proof + raw z+q polynomials (bridge phase).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "chipmunk.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"
#include "chipmunk_lrs.h"
#include "chipmunk_fq6_ext.h"
#include "chipmunk_mring_params.h"
#include "chipmunk_pedersen.h"
#include "chipmunk_bdlop.h"
#include "lotrs_params.h"
#include "chipmunk_fri.h"
#include "chipmunk_fri_transcript.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Parameters
 * ---------------------------------------------------------------------- */

/* Maximum polynomial dimension (struct-embedded arrays sized to this). */
#define CHIPMUNK_STARK_MAX_D        512

/* Polynomial commitment */
#define CHIPMUNK_STARK_COMMIT_BYTES 32      /* SHA3-256 hash of coefficients */

/* Number of random F_q points for quotient relation verification.
 * Each check: z(beta) == q(beta) * (beta - alpha_scalar) where alpha_scalar
 * is the Y^0 component of the extension challenge.
 * Soundness per check: ~2*Q^{-1} ~ 2^{-21.6} (two evaluation points).
 * With 11 checks: 11 * 21.6 ~ 238 bits >> 128 bits.
 * The extension alpha provides the primary ~129-bit soundness bound. */
#define CHIPMUNK_STARK_QUOTIENT_CHECKS  11

/* Opening proof: z and q polynomials sent in full (bridge phase).
 * Each polynomial = CHIPMUNK_STARK_MAX_D * sizeof(int32_t) = 2048 bytes.
 * Total opening: 4096 bytes. Phase 9.12+ will replace with DEEP composition. */
#define CHIPMUNK_STARK_OPENING_POLYS    2   /* z + q */
#define CHIPMUNK_STARK_OPENING_BYTES    \
    (CHIPMUNK_STARK_OPENING_POLYS * CHIPMUNK_STARK_MAX_D * (int)sizeof(int32_t))

/* FRI domain separator for STARK integration (exactly 16 bytes, no NUL). */
#define CHIPMUNK_STARK_FRI_DOMAIN    "CHIPMUNK-STARK-F"

/* Maximum proof size. */
#define CHIPMUNK_STARK_PROOF_MAX    (sizeof(chipmunk_stark_proof_t))

/* -------------------------------------------------------------------------
 * STARK Parameter Set (Phase 9.13 — Universal STARK)
 *
 * Encapsulates all runtime parameters for a specific (d, q) pair.
 * Each STARK context owns one params instance. The params struct
 * contains derived constants and per-context NTT twiddle tables.
 *
 * Predefined sets: chipmunk_stark_params_lrs(), _ring(), _test().
 * Custom sets: chipmunk_stark_params_init(&params, d, q).
 * ---------------------------------------------------------------------- */

/* FRI security parameters (same for all param sets). */
#define CHIPMUNK_STARK_FRI_FINAL_SIZE   16u
#define CHIPMUNK_STARK_FRI_NUM_QUERIES  8u
#define CHIPMUNK_STARK_FRI_GRINDING     16u
#define CHIPMUNK_STARK_FRI_CAP_SIZE     16u

/* Extension degree for Phi_9 = Y^6+Y^3+1. */
#define CHIPMUNK_STARK_EXT_DEG          6

typedef struct chipmunk_stark_params {
    /* --- Fundamental parameters --- */
    uint32_t d;             /* Polynomial ring dimension R_q = Z_q[X]/(X^d+1) */
    uint64_t q;             /* Modulus (must be prime, q mod 9 ∈ {2,5}) */

    /* --- Derived FRI constants --- */
    uint32_t fri_init_size; /* FRI domain = 4*d */
    uint32_t fri_rounds;    /* log2(4d) - log2(FINAL_SIZE) */
    uint32_t fri_total_data;/* Sum of all round sizes + final */

    /* --- Derived RS constants --- */
    uint32_t rs_msg_len;    /* RS message length = d */
    uint32_t rs_code_len;   /* RS codeword length = 4*d */
    int32_t  rs_coset_g;    /* Coset generator g for RS encoding */

    /* --- Field constants for this q --- */
    int32_t  omega;         /* Primitive 4d-th root of unity in F_q */
    int32_t  omega_inv;     /* omega^{-1} mod q */
    int32_t  inv_2;         /* 2^{-1} mod q */
    int32_t  inv_d;         /* d^{-1} mod q (for INTT scaling) */

    /* --- Per-context NTT twiddle tables (heap-allocated) --- */
    int32_t *zetas;         /* omega^k for k=0..4d-1 */
    int32_t *zetas_inv;     /* omega^{-k} for k=0..4d-1 */
    uint32_t zetas_size;    /* Number of elements (4d) */

    /* --- Parameter identification --- */
    uint32_t param_id;      /* SHA3-256(d||q)[0..3] for context matching */
} chipmunk_stark_params_t;

/**
 * Initialize STARK params for a given (d, q).
 * Computes omega, inv_2, inv_d, coset_g, twiddle tables.
 * Validates: q prime, q mod 9 ∈ {2,5}, 2-adicity(q-1) ≥ log2(4d)+1.
 * @param params Output params.
 * @param d      Polynomial ring dimension (must be power of 2, ≤ 512).
 * @param q      Modulus.
 * @return 0 on success, negative on error.
 */
int chipmunk_stark_params_init(chipmunk_stark_params_t *params,
                                uint32_t d, uint64_t q);

/**
 * Free heap resources in params (twiddle tables).
 * @param params Params to free.
 */
void chipmunk_stark_params_free(chipmunk_stark_params_t *params);

/**
 * Predefined LRS param set: d=512, q=3168257.
 * Backward compatible with Phase 9.1-9.12.
 */
const chipmunk_stark_params_t *chipmunk_stark_params_lrs(void);

/**
 * Predefined Ring param set: d=128, q=4206593.
 * For chipmunk_ring integration (Phase 9.13g).
 */
const chipmunk_stark_params_t *chipmunk_stark_params_ring(void);

/**
 * Predefined Test param set: d=32, q=4206593.
 * For fast unit tests.
 */
const chipmunk_stark_params_t *chipmunk_stark_params_test(void);

/* -------------------------------------------------------------------------
 * Types
 * ---------------------------------------------------------------------- */

/* Polynomial commitment: SHA3-256 hash of serialized coefficients */
typedef struct chipmunk_stark_commit {
    uint8_t hash[CHIPMUNK_STARK_COMMIT_BYTES];
} chipmunk_stark_commit_t;

/* Ring membership statement */
typedef struct chipmunk_stark_statement {
    const chipmunk_lrs_public_key_t *ring;      /* Ring of public keys */
    size_t ring_size;                           /* N */
    const uint8_t *message;                     /* Message being signed */
    size_t message_size;
    chipmunk_stark_commit_t ring_commit;        /* Commitment to ring hash */
} chipmunk_stark_statement_t;

/* Ring membership witness (private) */
typedef struct chipmunk_stark_witness {
    uint32_t signer_index;                      /* Which key signed */
    chipmunk_poly_t secret_key[CHIPMUNK_LRS_K * 2]; /* Secret witness (s0[GAMMA]+s1[GAMMA]) */
    chipmunk_poly_t indicator;                  /* b in {0,1}^N */
} chipmunk_stark_witness_t;

/* Full STARK proof. */
typedef struct chipmunk_stark_proof {
    /* Commitment phase */
    chipmunk_stark_commit_t w_commit;           /* Commitment to witness polynomial */
    chipmunk_stark_commit_t z_commit;           /* Commitment to constraint polynomial */
    chipmunk_stark_commit_t q_commit;           /* Commitment to quotient polynomial */
    chipmunk_stark_commit_t r_commit;           /* Commitment to randomizer (F_q^6) */

    /* Opening proof: serialized z and q polynomials for algebraic checks.
     * Retained alongside FRI proof for z(alpha)=0 and quotient verification. */
    uint8_t opening_proof[CHIPMUNK_STARK_OPENING_BYTES];
    size_t opening_proof_size;

    /* FRI proof: commits to q(X) with Fiat-Shamir transcript binding. */
    chipmunk_fri_proof_t   fri_proof;           /* FRI commit + query openings */
    uint32_t               fri_grinding_nonce;  /* Grinding PoW nonce */

    /* Phase 3 (P0-2 fix): FRI commitment to indicator polynomial b.
     *
     * The indicator b ∈ {0,1}^N is one-hot (b[signer_index]=1).
     * Previously b was NOT committed, allowing z≡0 forge (any proof passes
     * for any ring). Now b is committed via FRI and opened at the same
     * query points as q, so the verifier can check:
     *   z(point) = b(point)·(b(point)−1) + r·(Σb − 1) =? q(point)·(point − α)
     * Forging z≡0 now requires b·(b−1)+r·(Σb−1) ≡ 0 at all query points,
     * which (by Schwartz-Zippel) only holds if b is genuinely binary
     * with exactly one 1.
     *
     * Privacy: 8 query openings leak at most 8/512 ≈ 1.6% probability
     * of revealing whether a given position is the signer's. */
    chipmunk_fri_proof_t   b_fri_proof;         /* FRI commit + query openings for b */
    int32_t                b_values_at_queries[CHIPMUNK_FRI_NUM_QUERIES]; /* b at 8 query points */
    /* b_at_one: evaluation b(1) where 1 = omega_512^0 ∈ H.
     * Verifier checks b_at_one = inv(512) mod q, which enforces Σb(ω^i)=1.
     * This replaces the prover-asserted b_sum — now bound to FRI commitment. */
    int32_t                b_at_one;             /* b(X) at X=1, must equal 1/512 */

    /* Quotient polynomial q1 = C1/Z_H where C1(X)=b(X)·(b(X)−1), Z_H(X)=X^512−1.
     * FRI committed alongside b. Verifier checks: b(r)·(b(r)−1) = Z_H(r)·q1(r). */
    chipmunk_fri_proof_t   q1_fri_proof;        /* FRI commit + query openings for q1 */
    int32_t                q1_values_at_queries[CHIPMUNK_FRI_NUM_QUERIES]; /* q1 at 8 query points */

    /* Phase 3 FIX 6: Lattice binding via BDLOP opening.
     *
     * Prover commits lattice secret x via BDLOP (hiding commitment).
     * BDLOP opening proves knowledge of x AND linear constraint:
     *   A_pk · x = Σ_i b(ω^i) · P_i   (Module-LWE ring binding)
     *
     * This binds the STARK indicator to actual lattice key ownership
     * WITHOUT revealing which ring member is the signer (x is hidden
     * inside BDLOP commitment, only opened through Sigma protocol).
     *
     * Uses shared Pedersen params from the anon ledger context. */
    chipmunk_bdlop_commit_t    lattice_commit;  /* BDLOP commitment to x (K polynomials) */
    chipmunk_bdlop_proof_t     lattice_proof;   /* BDLOP opening: knowledge of x + A_pk·x=R */

    /* QROM transcript hash */
    uint8_t transcript_hash[32];
} chipmunk_stark_proof_t;

/* STARK context (public parameters) */
typedef struct chipmunk_stark_ctx {
    chipmunk_stark_params_t sp;                 /* Runtime parameters (d, q, FRI, NTT) */
    lotrs_params_t params;                      /* LoTRS lattice parameters */
    uint8_t domain_separator[32];               /* QROM domain separator */
    chipmunk_pedersen_params_t pedersen_params;  /* Phase 3 FIX 6: BDLOP lattice binding */
    bool initialized;
} chipmunk_stark_ctx_t;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/**
 * Initialize STARK context with public parameters.
 * @param ctx Output context.
 * @return 0 on success, negative on error.
 */
int chipmunk_stark_init(chipmunk_stark_ctx_t *ctx);

/**
 * Commit to a polynomial: C = H(f_0 || f_1 || ... || f_{N-1}).
 * Uses LRS default parameters (d=CHIPMUNK_STARK_MAX_D, q=CHIPMUNK_Q).
 * For non-LRS parameter sets, use chipmunk_stark_commit_ctx().
 * @param commit Output commitment.
 * @param poly Polynomial to commit.
 * @return 0 on success.
 */
int chipmunk_stark_commit(chipmunk_stark_commit_t *commit,
                          const chipmunk_poly_t *poly);

/**
 * Commit to a polynomial using the (d, q) from a STARK context.
 * This is the context-aware variant — required for param sets where
 * d != CHIPMUNK_STARK_MAX_D or q != CHIPMUNK_Q.
 * @param commit Output commitment.
 * @param ctx STARK context (provides d and q).
 * @param poly Polynomial to commit.
 * @return 0 on success, -EINVAL on null args.
 */
int chipmunk_stark_commit_ctx(chipmunk_stark_commit_t *commit,
                                const chipmunk_stark_ctx_t *ctx,
                                const chipmunk_poly_t *poly);

/**
 * Generate a ring membership STARK proof.
 *
 * Proves: "I know sk_j for pk_j in {pk_0, ..., pk_{N-1}}" without
 * revealing j.
 *
 * @param proof Output proof.
 * @param ctx STARK context.
 * @param statement Public statement (ring, message).
 * @param witness Private witness (secret key, index).
 * @return 0 on success, negative on error.
 */
int chipmunk_stark_prove(chipmunk_stark_proof_t *proof,
                         const chipmunk_stark_ctx_t *ctx,
                         const chipmunk_stark_statement_t *statement,
                         const chipmunk_stark_witness_t *witness);

/**
 * Verify a ring membership STARK proof.
 *
 * @param proof The proof to verify.
 * @param ctx STARK context.
 * @param statement Public statement (ring, message).
 * @return 1 if valid, 0 if invalid, negative on error.
 */
int chipmunk_stark_verify(const chipmunk_stark_proof_t *proof,
                          const chipmunk_stark_ctx_t *ctx,
                          const chipmunk_stark_statement_t *statement);

/**
 * Free STARK proof resources.
 */
void chipmunk_stark_proof_free(chipmunk_stark_proof_t *proof);

/**
 * Free STARK context resources.
 */
void chipmunk_stark_ctx_free(chipmunk_stark_ctx_t *ctx);

#ifdef __cplusplus
}
#endif
