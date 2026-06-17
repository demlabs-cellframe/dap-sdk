/*
 * Chipmunk Ring — non-interactive lattice-based ring signature.
 *
 * Based on LoTRS binary ring proof (RS) with Fiat-Shamir transform.
 * Single-signer, non-interactive, O(N) signature size.
 *
 * Wire format:
 *   Header (28B) + B_bin_hi (n̂ polys) + x_seed (16B) + f1 ((β-1) polys) + z_b ((n̂+k̂) polys)
 *
 * All coefficients Rice-coded for compact serialization.
 */

#pragma once
#ifndef _CHIPMUNK_RING_V2_H_
#define _CHIPMUNK_RING_V2_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "lotrs_params.h"
#include "lotrs_ring.h"
#include "lotrs_codec.h"

/* --- Key pair --- */
typedef struct chipmunk_ring_v2_pk {
    lotrs_polyvec_t a_hat;  /* k polynomials */
} chipmunk_ring_v2_pk_t;

typedef struct chipmunk_ring_v2_sk {
    lotrs_polyvec_t s;      /* l+k polynomials, short */
} chipmunk_ring_v2_sk_t;

typedef struct chipmunk_ring_v2_keypair {
    chipmunk_ring_v2_pk_t pk;
    chipmunk_ring_v2_sk_t sk;
} chipmunk_ring_v2_keypair_t;

/* --- Ring public key table --- */
typedef struct chipmunk_ring_v2_ring {
    chipmunk_ring_v2_pk_t *pks;  /* N public keys */
    uint32_t N;
} chipmunk_ring_v2_ring_t;

/* --- Signature --- */
typedef struct chipmunk_ring_v2_sig {
    uint8_t *data;
    size_t   len;
} chipmunk_ring_v2_sig_t;

/* --- Wire header --- */
typedef struct chipmunk_ring_v2_header {
    uint32_t magic;
    uint32_t version;
    uint32_t d;
    uint32_t N;
    uint32_t rice_k;       /* Rice parameter for coefficient coding */
    uint32_t flags;
} chipmunk_ring_v2_header_t;

#define CHIPMUNK_RING_V2_MAGIC    0x43525632u  /* 'CRV2' LE */
#define CHIPMUNK_RING_V2_VERSION  1u
#define CHIPMUNK_RING_V2_HEADER_BYTES 24u

/* --- API --- */

int chipmunk_ring_v2_keygen(chipmunk_ring_v2_keypair_t *a_kp,
                            const lotrs_params_t *a_par,
                            const uint8_t a_seed[32]);

int chipmunk_ring_v2_sign(chipmunk_ring_v2_sig_t *a_sig,
                          const lotrs_params_t *a_par,
                          const chipmunk_ring_v2_ring_t *a_ring,
                          const chipmunk_ring_v2_sk_t *a_sk,
                          uint32_t a_signer_idx,
                          const uint8_t *a_msg, size_t a_msg_len,
                          const uint8_t a_seed[32]);

int chipmunk_ring_v2_verify(const chipmunk_ring_v2_sig_t *a_sig,
                            const lotrs_params_t *a_par,
                            const chipmunk_ring_v2_ring_t *a_ring,
                            const uint8_t *a_msg, size_t a_msg_len);

void chipmunk_ring_v2_keypair_free(chipmunk_ring_v2_keypair_t *a_kp);
void chipmunk_ring_v2_ring_free(chipmunk_ring_v2_ring_t *a_ring);
void chipmunk_ring_v2_sig_free(chipmunk_ring_v2_sig_t *a_sig);

/* Wire size for given parameters. */
size_t chipmunk_ring_v2_sig_bytes(const lotrs_params_t *a_par);

#endif /* _CHIPMUNK_RING_V2_H_ */
