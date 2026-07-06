/*
 * Chipmunk Ring — non-interactive lattice-based ring signature.
 *
 * Based on LoTRS binary ring proof (RS) with Fiat-Shamir transform.
 * Single-signer, non-interactive, O(N) signature size.
 *
 * Wire format:
 *   Header (32B) + N×T (k polys each) + N×c (1 poly each) + N×z (l+k polys each)
 */

#pragma once
#ifndef _CHIPMUNK_RING_H_
#define _CHIPMUNK_RING_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "lotrs_params.h"
#include "lotrs_ring.h"
#include "lotrs_codec.h"
#include "dap_serialize.h"

/* --- Key pair --- */
typedef struct chipmunk_ring_pk {
    lotrs_polyvec_t a_hat;  /* k polynomials */
} chipmunk_ring_pk_t;

typedef struct chipmunk_ring_sk {
    lotrs_polyvec_t s;      /* l+k polynomials, short */
} chipmunk_ring_sk_t;

typedef struct chipmunk_ring_keypair {
    chipmunk_ring_pk_t pk;
    chipmunk_ring_sk_t sk;
} chipmunk_ring_keypair_t;

/* --- Ring public key table --- */
typedef struct chipmunk_ring_table {
    chipmunk_ring_pk_t *pks;  /* N public keys */
    uint32_t N;
} chipmunk_ring_table_t;

/* --- Signature --- */
typedef struct chipmunk_ring_sig {
    uint8_t *data;
    size_t   len;
} chipmunk_ring_sig_t;

/* --- Wire header --- */
typedef struct chipmunk_ring_header {
    uint32_t magic;
    uint32_t version;
    uint32_t d;
    uint32_t N;
    uint32_t rice_k_z;     /* Rice parameter for z coefficient coding */
    int64_t  rice_bound_z; /* Centered bound for z coefficients (φ·η) */
    uint32_t flags;
    uint8_t  param_hash[16]; /* Truncated hash of parameter set */
    uint32_t key_image_len;                /* Actual key image byte count (k * poly_bytes) */
    uint8_t  key_image[9216];              /* Linkability key image (k q-packed polynomials) */
} chipmunk_ring_header_t;

#define CHIPMUNK_RING_MAGIC    0x4352494Eu  /* 'CRIN' LE */
#define CHIPMUNK_RING_VERSION  2u
#define CHIPMUNK_RING_N_MIN    8u           /* Hard minimum ring size for anonymity */

/* Header schema for dap_serialize. */
extern const dap_serialize_schema_t s_chipmunk_ring_header_schema;

/* Header wire size (calculated from schema). */
static inline size_t chipmunk_ring_header_bytes(void)
{
    return dap_serialize_calc_size_raw(&s_chipmunk_ring_header_schema, NULL, NULL, NULL);
}

/* --- API --- */

int chipmunk_ring_keygen(chipmunk_ring_keypair_t *a_kp,
                         const lotrs_params_t *a_par,
                         const uint8_t a_seed[32]);

int chipmunk_ring_sign(chipmunk_ring_sig_t *a_sig,
                       const lotrs_params_t *a_par,
                       const chipmunk_ring_table_t *a_ring,
                       const chipmunk_ring_sk_t *a_sk,
                       uint32_t a_signer_idx,
                       const uint8_t *a_msg, size_t a_msg_len,
                       const uint8_t a_seed[32]);

int chipmunk_ring_verify(const chipmunk_ring_sig_t *a_sig,
                         const lotrs_params_t *a_par,
                         const chipmunk_ring_table_t *a_ring,
                         const uint8_t *a_msg, size_t a_msg_len);

void chipmunk_ring_keypair_free(chipmunk_ring_keypair_t *a_kp);
void chipmunk_ring_table_free(chipmunk_ring_table_t *a_ring);
void chipmunk_ring_sig_free(chipmunk_ring_sig_t *a_sig);

/* Linkability: compare key images of two signatures.
 * Returns 1 if same signer (key images match), 0 if different, negative on error. */
int chipmunk_ring_link(const chipmunk_ring_sig_t *a_sig1, const chipmunk_ring_sig_t *a_sig2);

/* =========================================================================
 * Anonymous Ring Signatures via MRNG (threshold=1, O(log N) size)
 *
 * Uses MRNG's algebraic aggregation + halving fold for logarithmic-size
 * single-signer anonymous ring signatures.
 * ========================================================================= */

#include "chipmunk_lrs.h"
#include "chipmunk_mring.h"

/* Generate anonymous ring signature (threshold=1, O(log N) size). */
chipmunk_ring_error_t chipmunk_ring_sign_anonymous(
    uint8_t **a_out_buf, size_t *a_out_size,
    const chipmunk_lrs_secret_key_t *a_signer_sk,
    const chipmunk_lrs_public_key_t *a_ring,
    size_t a_ring_size,
    const uint8_t *a_message, size_t a_message_size,
    const uint8_t a_randomness_seed[32]);

/* Verify anonymous ring signature. */
chipmunk_ring_error_t chipmunk_ring_verify_anonymous(
    const uint8_t *a_buf, size_t a_buf_size,
    const chipmunk_lrs_public_key_t *a_ring,
    size_t a_ring_size,
    const uint8_t *a_message, size_t a_message_size);

/* Linkability for anonymous ring signatures (compare link tags). */
int chipmunk_ring_link_anonymous(
    const uint8_t *a_buf1, size_t a_buf_size1,
    const uint8_t *a_buf2, size_t a_buf_size2);

/* Upper bound on wire size for given parameters (raw packing, before Rice coding). */
size_t chipmunk_ring_sig_bytes_max(const lotrs_params_t *a_par, uint32_t a_N);

#endif /* _CHIPMUNK_RING_H_ */
