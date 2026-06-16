/*
 * dap_sign_threshold_ring.h — Common API for threshold ring signatures.
 *
 * Provides a unified interface for schemes that implement t-of-N
 * threshold ring signatures (MRNG, LoTRS, etc.).
 *
 * Usage:
 *   1. dap_sign_threshold_ring_keygen() — generate keypair
 *   2. dap_sign_threshold_ring_sign()   — create t-of-N signature
 *   3. dap_sign_threshold_ring_verify() — verify signature
 */

#pragma once
#ifndef _DAP_SIGN_THRESHOLD_RING_H_
#define _DAP_SIGN_THRESHOLD_RING_H_

#include <stddef.h>
#include <stdint.h>

#include "dap_sign.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dap_sign_threshold_ring_alg {
    DAP_SIGN_THRESHOLD_RING_MRNG,   /* Chipmunk MRNG — log-N fold */
    DAP_SIGN_THRESHOLD_RING_LOTRS,  /* LoTRS — lattice threshold ring */
} dap_sign_threshold_ring_alg_t;

typedef struct dap_sign_threshold_ring_keypair {
    uint8_t *pk;       /* public key bytes */
    size_t   pk_len;
    uint8_t *sk;       /* secret key bytes */
    size_t   sk_len;
    dap_sign_threshold_ring_alg_t alg;
} dap_sign_threshold_ring_keypair_t;

/*
 * Generate a keypair for the given algorithm.
 * Caller owns *a_pk and *a_sk — free with dap_sign_threshold_ring_keypair_free().
 */
int dap_sign_threshold_ring_keygen(dap_sign_threshold_ring_keypair_t *a_kp,
                                   dap_sign_threshold_ring_alg_t a_alg);

/*
 * Create a t-of-N threshold ring signature.
 *
 * @param a_out       Receives allocated dap_sign_t. Caller owns.
 * @param a_alg       Algorithm to use (MRNG or LOTRS).
 * @param a_sks       Array of t secret key byte buffers.
 * @param a_sks_lens  Array of t secret key lengths.
 * @param a_ring      Array of N public key byte buffers.
 * @param a_ring_lens Array of N public key lengths.
 * @param a_n_ring    Ring size N.
 * @param a_threshold Threshold t.
 * @param a_msg       Message to sign.
 * @param a_msg_len   Message length.
 * @param a_ctx       Optional context (NULL if unused).
 * @param a_ctx_len   Context length (0 if unused).
 * @param a_seed      Randomness seed (32 bytes per signer).
 *
 * @return 0 on success, negative errno on failure.
 */
int dap_sign_threshold_ring_sign(dap_sign_t **a_out,
                                 dap_sign_threshold_ring_alg_t a_alg,
                                 const uint8_t **a_sks, const size_t *a_sks_lens,
                                 const uint8_t **a_ring, const size_t *a_ring_lens,
                                 size_t a_n_ring, uint32_t a_threshold,
                                 const uint8_t *a_msg, size_t a_msg_len,
                                 const void *a_ctx, size_t a_ctx_len,
                                 const uint8_t *a_seed);

/*
 * Verify a threshold ring signature.
 *
 * @param a_sign      The signature to verify.
 * @param a_ring      Array of N public key byte buffers.
 * @param a_ring_lens Array of N public key lengths.
 * @param a_n_ring    Ring size N.
 * @param a_msg       Message.
 * @param a_msg_len   Message length.
 * @param a_ctx       Optional context (NULL if unused).
 * @param a_ctx_len   Context length (0 if unused).
 *
 * @return 0 on success, negative errno on failure.
 */
int dap_sign_threshold_ring_verify(const dap_sign_t *a_sign,
                                   const uint8_t **a_ring, const size_t *a_ring_lens,
                                   size_t a_n_ring,
                                   const uint8_t *a_msg, size_t a_msg_len,
                                   const void *a_ctx, size_t a_ctx_len);

void dap_sign_threshold_ring_keypair_free(dap_sign_threshold_ring_keypair_t *a_kp);

#ifdef __cplusplus
}
#endif

#endif /* _DAP_SIGN_THRESHOLD_RING_H_ */
