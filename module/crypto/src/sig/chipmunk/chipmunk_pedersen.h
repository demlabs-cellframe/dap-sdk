/*
 * chipmunk_pedersen.h — Lattice-based Pedersen commitments for confidential amounts.
 *
 * Commitment: C = A * r + encode(m) mod q
 * where A is a public matrix, r is a random blinding vector, m is a uint256 amount.
 *
 * Amount encoding (Phase 6 fix — scalar, perfectly additive):
 *   encode(v) has ALL 512 coefficients equal to (v mod Q).
 *   This is Z-linear: encode(v1) + encode(v2) = encode(v1 + v2) in R_q,
 *   and encode(v1) - encode(v2) = encode(v1 - v2) in R_q.
 *   No carry propagation issues at any value.
 *
 *   Value range: [0, Q-1] where Q = 3168257 (~3.1M atomic units per TX).
 *   TX construction must reject amounts >= Q.
 *   Range proofs prove v ∈ [0, Q-1] via bit decomposition with
 *   powers-of-2 weighting mod Q.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "chipmunk.h"
#include "chipmunk_poly.h"
#include "chipmunk_lrs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHIPMUNK_PEDERSEN_K         6
#define CHIPMUNK_PEDERSEN_COMMIT_BYTES  (CHIPMUNK_PEDERSEN_K * 1408)
#define CHIPMUNK_PEDERSEN_VALUE_BYTES   32
#define CHIPMUNK_PEDERSEN_VALUE_BITS    256

/* Encoding parameters (Phase 6: scalar encoding)
 * encode(v)[i] = (v mod Q) for ALL i in [0, N).
 * Value range: [0, Q-1] = [0, 3168256].
 * 256 bits of value bytes supported; only lower 64 bits used.
 * Digit constants kept for range proof API compatibility. */
#define CHIPMUNK_PEDERSEN_CHUNKS        19       /* legacy: unused by scalar encoding */
#define CHIPMUNK_PEDERSEN_CHUNK_MAX     16383    /* legacy: unused by scalar encoding */

/* Legacy digit API — kept for range proof compatibility.
 * Range proofs use commit_explicit_digit with powers-of-2 mod Q. */
#define CHIPMUNK_PEDERSEN_DIGIT_BITS    14       /* legacy (unused) */
#define CHIPMUNK_PEDERSEN_DIGIT_BASE    16384    /* legacy (unused) */
#define CHIPMUNK_PEDERSEN_DIGITS        CHIPMUNK_PEDERSEN_CHUNKS
#define CHIPMUNK_PEDERSEN_DIGIT_MAX     CHIPMUNK_PEDERSEN_CHUNK_MAX

typedef struct chipmunk_pedersen_commit {
    chipmunk_poly_t C[CHIPMUNK_PEDERSEN_K];
} chipmunk_pedersen_commit_t;

typedef struct chipmunk_pedersen_params {
    chipmunk_poly_t A[CHIPMUNK_PEDERSEN_K][CHIPMUNK_LRS_K];
    uint64_t q;       /* Field modulus (Phase 9.14c). Defaults to CHIPMUNK_Q. */
    bool initialized;
} chipmunk_pedersen_params_t;

typedef struct chipmunk_pedersen_opening {
    uint8_t message[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    chipmunk_poly_t randomness[CHIPMUNK_LRS_K];
} chipmunk_pedersen_opening_t;

int chipmunk_pedersen_init(chipmunk_pedersen_params_t *params,
                           const uint8_t seed[32]);

/**
 * Commit to a 256-bit atomic amount (LE bytes).
 * Value is encoded scalarly: encode(v)[i] = (v mod Q) for all i.
 * This encoding is Z-linear: encode(v1) + encode(v2) = encode(v1+v2) in R_q.
 * Amount must be in [0, Q-1]; caller must validate.
 */
int chipmunk_pedersen_commit(chipmunk_pedersen_commit_t *commit,
                             const chipmunk_pedersen_params_t *params,
                             const uint8_t message[CHIPMUNK_PEDERSEN_VALUE_BYTES],
                             const uint8_t randomness_seed[32]);

/**
 * Commit to a single bit (0/1) at coefficient @a bit_pos (used by range proofs).
 * @deprecated Use chipmunk_pedersen_commit_explicit_digit for digit-based encoding.
 */
int chipmunk_pedersen_commit_explicit_bit(chipmunk_pedersen_commit_t *commit,
                                          const chipmunk_pedersen_params_t *params,
                                          uint8_t bit,
                                          uint32_t bit_pos,
                                          const chipmunk_poly_t randomness[CHIPMUNK_LRS_K]);

/**
 * Commit to a scalar value (mod Q) at all coefficients.
 * Used by range proofs: commit(b_i * 2^i mod Q, r_i).
 * a_digit_pos is ignored (scalar encoding is position-independent).
 */
int chipmunk_pedersen_commit_explicit_digit(chipmunk_pedersen_commit_t *commit,
                                             const chipmunk_pedersen_params_t *params,
                                             int32_t digit,
                                             uint32_t digit_pos,
                                             const chipmunk_poly_t randomness[CHIPMUNK_LRS_K]);

int chipmunk_pedersen_verify_opening(const chipmunk_pedersen_commit_t *commit,
                                     const chipmunk_pedersen_params_t *params,
                                     const chipmunk_pedersen_opening_t *opening);

void chipmunk_pedersen_add(chipmunk_pedersen_commit_t *sum,
                           const chipmunk_pedersen_commit_t *c1,
                           const chipmunk_pedersen_commit_t *c2);

/** @brief Per-q variant of chipmunk_pedersen_add (Phase 9.14c). */
void chipmunk_pedersen_add_q(chipmunk_pedersen_commit_t *sum,
                               const chipmunk_pedersen_commit_t *c1,
                               const chipmunk_pedersen_commit_t *c2,
                               uint64_t q);

/**
 * Derive blinding polynomial vector from a 32-byte seed.
 * Extracts the deterministic SHAKE256-based derivation used internally
 * by chipmunk_pedersen_commit (seed || "pedersen-randomness-v1").
 */
int chipmunk_pedersen_derive_blinding(chipmunk_poly_t r[CHIPMUNK_LRS_K],
                                       const uint8_t randomness_seed[32]);

/**
 * Commit to a 256-bit amount with explicit blinding polynomials.
 * Same as chipmunk_pedersen_commit but accepts pre-derived randomness
 * instead of deriving from a seed.
 */
int chipmunk_pedersen_commit_explicit(chipmunk_pedersen_commit_t *commit,
                                       const chipmunk_pedersen_params_t *params,
                                       const uint8_t message[CHIPMUNK_PEDERSEN_VALUE_BYTES],
                                       const chipmunk_poly_t randomness[CHIPMUNK_LRS_K]);

/**
 * Subtract blinding vectors: result[j] = a[j] - b[j]  (mod q), for each j in [0, LRS_K).
 */
void chipmunk_pedersen_blinding_sub(chipmunk_poly_t result[CHIPMUNK_LRS_K],
                                      const chipmunk_poly_t a[CHIPMUNK_LRS_K],
                                      const chipmunk_poly_t b[CHIPMUNK_LRS_K]);

int chipmunk_pedersen_commit_serialize(uint8_t *a_out, size_t a_out_size,
                                       const chipmunk_pedersen_commit_t *commit);

int chipmunk_pedersen_commit_deserialize(chipmunk_pedersen_commit_t *commit,
                                         const uint8_t *a_in, size_t a_in_size);

/** @brief Per-q variant of chipmunk_pedersen_commit_deserialize (Phase 9.14c). */
int chipmunk_pedersen_commit_deserialize_q(chipmunk_pedersen_commit_t *commit,
                                             const uint8_t *a_in, size_t a_in_size,
                                             uint64_t q);

#ifdef __cplusplus
}
#endif
