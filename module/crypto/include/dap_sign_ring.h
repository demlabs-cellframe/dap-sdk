/*
 * dap_sign_ring.h — Unified ring signature API.
 *
 * Common interface for all ring signature schemes:
 *   - MRNG:  log-N threshold ring (t-of-N, non-interactive)
 *   - LRS:   1-of-N linkable ring (non-interactive)
 *   - LoTRS: lattice threshold ring (t-of-N, interactive)
 *
 * Non-interactive schemes: single signer produces full signature.
 * Interactive schemes: T signers communicate over multiple rounds.
 *
 * For non-threshold schemes (LRS), threshold > 1 returns -EINVAL.
 * For interactive schemes, use dap_sign_ring_session_* API.
 */

#pragma once
#ifndef _DAP_SIGN_RING_H_
#define _DAP_SIGN_RING_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dap_sign.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Algorithm identifiers --- */

typedef enum dap_sign_ring_alg {
    DAP_SIGN_RING_MRNG,    /* Chipmunk MRNG — log-N fold, t-of-N, non-interactive */
    DAP_SIGN_RING_LRS,     /* Chipmunk LRS — 1-of-N linkable, non-interactive */
    DAP_SIGN_RING_LOTRS,   /* LoTRS — lattice t-of-N, interactive */
} dap_sign_ring_alg_t;

/* --- Ring signature parameters --- */

typedef struct dap_sign_ring_params {
    dap_sign_ring_alg_t alg;
    uint32_t            ring_size;      /* N */
    uint32_t            threshold;      /* t (must be 1 for LRS) */
    bool                interactive;    /* true for multi-round schemes */
} dap_sign_ring_params_t;

/* --- Non-interactive API (MRNG, LRS) --- */

/*
 * Create a ring signature (non-interactive).
 *
 * For interactive schemes (LoTRS), returns -EINVAL.
 * Use dap_sign_ring_session_* for interactive schemes.
 *
 * @param a_out       Receives allocated dap_sign_t. Caller owns.
 * @param a_params    Ring parameters (alg, ring_size, threshold).
 * @param a_sks       Array of threshold secret key buffers.
 * @param a_sks_lens  Array of threshold secret key lengths.
 * @param a_ring      Array of ring_size public key buffers.
 * @param a_ring_lens Array of ring_size public key lengths.
 * @param a_msg       Message bytes.
 * @param a_msg_len   Message length.
 * @param a_ctx       Optional context (NULL if unused).
 * @param a_ctx_len   Context length.
 * @param a_seed      Randomness seed.
 *
 * @return 0 on success, negative errno on failure.
 */
int dap_sign_ring_create(dap_sign_t **a_out,
                         const dap_sign_ring_params_t *a_params,
                         const uint8_t **a_sks, const size_t *a_sks_lens,
                         const uint8_t **a_ring, const size_t *a_ring_lens,
                         const uint8_t *a_msg, size_t a_msg_len,
                         const void *a_ctx, size_t a_ctx_len,
                         const uint8_t *a_seed);

/*
 * Verify a ring signature.
 *
 * Works for both interactive and non-interactive signatures.
 *
 * @param a_sign      The signature to verify.
 * @param a_ring      Array of ring_size public key buffers.
 * @param a_ring_lens Array of ring_size public key lengths.
 * @param a_ring_size Ring size N.
 * @param a_msg       Message bytes.
 * @param a_msg_len   Message length.
 * @param a_ctx       Optional context (NULL if unused).
 * @param a_ctx_len   Context length.
 *
 * @return 0 on success, negative errno on failure.
 */
int dap_sign_ring_verify(const dap_sign_t *a_sign,
                         const uint8_t **a_ring, const size_t *a_ring_lens,
                         size_t a_ring_size,
                         const uint8_t *a_msg, size_t a_msg_len,
                         const void *a_ctx, size_t a_ctx_len);

/* --- Interactive session API (LoTRS) --- */

typedef struct dap_sign_ring_session dap_sign_ring_session_t;

/*
 * Create an interactive signing session.
 *
 * @param a_sess      Receives allocated session. Caller owns.
 * @param a_params    Ring parameters (must have interactive=true).
 * @param a_ring      Array of ring_size public key buffers.
 * @param a_ring_lens Array of ring_size public key lengths.
 * @param a_msg       Message bytes.
 * @param a_msg_len   Message length.
 *
 * @return 0 on success, negative errno on failure.
 */
int dap_sign_ring_session_create(dap_sign_ring_session_t **a_sess,
                                 const dap_sign_ring_params_t *a_params,
                                 const uint8_t **a_ring, const size_t *a_ring_lens,
                                 const uint8_t *a_msg, size_t a_msg_len);

/*
 * Perform one round of the interactive signing protocol.
 *
 * @param a_sess      The session.
 * @param a_sk        Signer's secret key buffer.
 * @param a_sk_len    Secret key length.
 * @param a_signer_idx Signer's index in the ring (0-based).
 * @param a_in        Input from previous round (NULL for first round).
 * @param a_in_len    Input length.
 * @param a_out       Receives allocated output for this round. Caller owns.
 * @param a_out_len   Output length.
 *
 * @return 0 on success, negative errno on failure.
 *         Returns 1 when signing is complete (a_out contains final signature).
 */
int dap_sign_ring_session_round(dap_sign_ring_session_t *a_sess,
                                const uint8_t *a_sk, size_t a_sk_len,
                                uint32_t a_signer_idx,
                                const uint8_t *a_in, size_t a_in_len,
                                uint8_t **a_out, size_t *a_out_len);

/*
 * Finalize the interactive signing session.
 *
 * @param a_sess  The session.
 * @param a_out   Receives allocated dap_sign_t. Caller owns.
 *
 * @return 0 on success, negative errno on failure.
 */
int dap_sign_ring_session_finish(dap_sign_ring_session_t *a_sess,
                                 dap_sign_t **a_out);

void dap_sign_ring_session_free(dap_sign_ring_session_t *a_sess);

/* --- Key generation --- */

/*
 * Generate a keypair for the given algorithm.
 * Caller owns *a_pk and *a_sk.
 */
int dap_sign_ring_keygen(dap_sign_ring_alg_t a_alg,
                         uint8_t **a_pk, size_t *a_pk_len,
                         uint8_t **a_sk, size_t *a_sk_len);

#ifdef __cplusplus
}
#endif

#endif /* _DAP_SIGN_RING_H_ */
