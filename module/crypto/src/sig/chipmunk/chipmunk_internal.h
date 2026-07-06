/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
 *
 * Chipmunk internal helpers shared between chipmunk.c, chipmunk_aggregation.c
 * and chipmunk_hypertree.c.  Not part of the public API surface — do NOT
 * expose the prototypes outside the Chipmunk implementation unit.
 */

#pragma once

#include <stdint.h>
#include "chipmunk.h"
#include "chipmunk_hots.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CR-D15.B (CR-D3 reuse): domain-separated SHA3-256 derivation of the
 * per-leaf HOTS secret key from the master key_seed and a monotonic
 * leaf_index.  The header shares it with chipmunk_hypertree.c so that the
 * hypertree keygen/sign pipeline stays bit-for-bit compatible with the
 * single-shot pipeline in chipmunk.c — any discrepancy would surface as a
 * path-verify failure against a root that was built from a different
 * derivation.
 *
 * Returns CHIPMUNK_ERROR_SUCCESS or a negative error code.  On success
 * a_hots_sk_out is populated in NTT domain (ready to feed to
 * chipmunk_hots_sign / dap_chipmunk_compute_hots_pk_internal).
 */
int dap_chipmunk_derive_hots_leaf_secret_internal(const uint8_t a_key_seed[32],
                                                  uint32_t a_leaf_index,
                                                  chipmunk_hots_sk_t *a_hots_sk_out);

/*
 * Compute the HOTS public key (v0, v1) from (A, s0, s1).  a_params->a[i]
 * MUST be in NTT domain (sampled + forward-NTT'd by the caller).  The
 * output v0/v1 live in coefficient (time) domain, matching the encoding
 * that chipmunk_hots_verify and chipmunk_hots_pk_to_hvc_poly both expect.
 */
void dap_chipmunk_compute_hots_pk_internal(const chipmunk_hots_params_t *a_params,
                                           const chipmunk_hots_sk_t *a_hots_sk,
                                           chipmunk_hots_pk_t *a_hots_pk_out);

#ifdef __cplusplus
}
#endif
