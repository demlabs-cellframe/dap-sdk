/*
 * chipmunk_fri_transcript.h — Fiat-Shamir transcript for FRI-DEEP PCS.
 *
 * Manages the Fiat-Shamir heuristic:
 *   - Absorb: commit data (Merkle caps, DEEP openings) into transcript
 *   - Squeeze: derive F_q challenges via SHAKE256 XOF
 *   - Grinding: 16-bit proof-of-work nonce search
 *
 * Transcript state is a SHAKE256 XOF instance. All challenges are
 * derived deterministically from absorbed data, ensuring non-malleability.
 *
 * Grinding adds 16 bits of computational soundness: the prover must
 * find nonce such that SHAKE256(state || nonce) has ≥ 16 leading
 * zero bits. Expected work: 2^16 ≈ 65536 hashes.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "chipmunk.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Grinding difficulty: number of leading zero bits required. */
#define CHIPMUNK_FRI_GRINDING_BITS   16u

/* Maximum transcript buffer size (absorbed data before squeeze). */
#define CHIPMUNK_FRI_TRANSCRIPT_BUF   4096u

/* FRI transcript challenge types. */
typedef enum {
    CHIPMUNK_FRI_CHAL_DEEP_Z = 0,      /**< DEEP evaluation point z ∈ F_q */
    CHIPMUNK_FRI_CHAL_DEEP_GAMMA,      /**< DEEP combination weight γ ∈ F_q */
    CHIPMUNK_FRI_CHAL_FRI_ALPHA,       /**< FRI round challenge α_r ∈ F_q */
    CHIPMUNK_FRI_CHAL_NUM_TYPES        /**< Total number of challenge types */
} chipmunk_fri_chal_type_t;

/* FRI transcript state. */
typedef struct chipmunk_fri_transcript {
    uint8_t  buffer[CHIPMUNK_FRI_TRANSCRIPT_BUF]; /**< Absorption buffer */
    size_t   buf_len;                   /**< Current buffer fill level */
    uint32_t grinding_nonce;            /**< PoW nonce found by prover */
    uint32_t squeeze_counter;           /**< Counter for squeeze calls */
    bool     grinding_done;             /**< True after grinding search */
    bool     initialized;               /**< True after init() */
} chipmunk_fri_transcript_t;

/**
 * Initialize transcript with domain separator.
 * @param tr     Transcript to initialize.
 * @param domain 16-byte domain separator (e.g. "CHIPMUNK-FRI-PCS").
 * @return 0 on success.
 */
int chipmunk_fri_transcript_init(chipmunk_fri_transcript_t *tr,
                                  const uint8_t domain[16]);

/**
 * Absorb bytes into transcript.
 * @param tr   Transcript.
 * @param data Data to absorb.
 * @param len  Length of data.
 * @return 0 on success, negative on error.
 */
int chipmunk_fri_transcript_absorb(chipmunk_fri_transcript_t *tr,
                                    const uint8_t *data, size_t len);

/**
 * Absorb an F_q field element (4 bytes, little-endian).
 * @param tr  Transcript.
 * @param val Field element ∈ [0, q).
 * @return 0 on success.
 */
int chipmunk_fri_transcript_absorb_fq(chipmunk_fri_transcript_t *tr,
                                       int32_t val);

/**
 * Absorb a Merkle cap (16 × 4-byte F_q elements = 64 bytes).
 * @param tr   Transcript.
 * @param cap  Pointer to 16 int32_t values (the cap nodes).
 * @param size Number of cap nodes (must be ≤ 16).
 * @return 0 on success.
 */
int chipmunk_fri_transcript_absorb_cap(chipmunk_fri_transcript_t *tr,
                                        const int32_t *cap, uint32_t size);

/**
 * Squeeze an F_q challenge from transcript using rejection sampling.
 * Repeatedly squeezes 4 bytes from XOF until value ∈ [0, q).
 * @param tr  Transcript.
 * @param out Output challenge ∈ [0, q).
 * @return 0 on success, negative on error.
 */
int chipmunk_fri_transcript_squeeze_fq(chipmunk_fri_transcript_t *tr,
                                        int32_t *out);

/**
 * Squeeze multiple F_q challenges.
 * @param tr    Transcript.
 * @param out   Output array.
 * @param count Number of challenges to squeeze.
 * @return 0 on success.
 */
int chipmunk_fri_transcript_squeeze_fq_many(chipmunk_fri_transcript_t *tr,
                                             int32_t out[], uint32_t count);

/**
 * Perform grinding PoW: find nonce such that leading bits of
 * SHAKE256(state || nonce) are zero.
 *
 * Prover: search for valid nonce (expected 2^GRINDING_BITS iterations).
 * Verifier: check that the stored nonce satisfies the constraint.
 *
 * @param tr    Transcript (must have data absorbed).
 * @param nonce Output: the found nonce (prover), or input to verify (verifier).
 * @return 0 on success (nonce found/verified).
 *         -1 if search failed (should not happen with enough attempts).
 */
int chipmunk_fri_transcript_grind(chipmunk_fri_transcript_t *tr,
                                   uint32_t *nonce);

/**
 * Verify grinding nonce (verifier-side).
 * @param tr    Transcript (must have same absorbed data as prover).
 * @param nonce Nonce to verify.
 * @return true if valid, false otherwise.
 */
bool chipmunk_fri_transcript_verify_grinding(const chipmunk_fri_transcript_t *tr,
                                              uint32_t nonce);

/**
 * Finalize transcript: flush buffer into XOF and perform grinding.
 * Must be called before squeeze operations.
 * @param tr Transcript.
 * @return 0 on success.
 */
int chipmunk_fri_transcript_finalize(chipmunk_fri_transcript_t *tr);

/**
 * Clone transcript state (for verifier reconstruction).
 * @param dst Destination.
 * @param src Source.
 * @return 0 on success.
 */
int chipmunk_fri_transcript_clone(chipmunk_fri_transcript_t *dst,
                                  const chipmunk_fri_transcript_t *src);

#ifdef __cplusplus
}
#endif
