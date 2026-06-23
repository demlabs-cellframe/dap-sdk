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
} chipmunk_ring_header_t;

#define CHIPMUNK_RING_MAGIC    0x43525632u  /* 'CRV2' LE */
#define CHIPMUNK_RING_VERSION  2u

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

/* Upper bound on wire size for given parameters (raw packing, before Rice coding). */
size_t chipmunk_ring_sig_bytes_max(const lotrs_params_t *a_par, uint32_t a_N);

#endif /* _CHIPMUNK_RING_H_ */
