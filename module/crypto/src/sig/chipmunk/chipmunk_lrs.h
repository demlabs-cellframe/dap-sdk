/*
 * Chipmunk LRS canonical C0/RB2 primitives.
 *
 * This module is the CR-11.D Phase-3 implementation base for the native
 * linkable ring signature design.  It intentionally does not reuse the
 * old chipmunk_ring_signature_t / Acorn-era wire objects and it has no
 * wire-version compatibility layer: C0/RB2 is the only canonical LRS
 * profile implemented here.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "chipmunk.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CHIPMUNK_LRS_MAGIC_CLPK 0x4b504c43u /* 'CLPK' little-endian */
#define CHIPMUNK_LRS_MAGIC_CLSK 0x4b534c43u /* 'CLSK' little-endian */
#define CHIPMUNK_LRS_MAGIC_CLRS 0x53524c43u /* 'CLRS' little-endian */
#define CHIPMUNK_LRS_MAGIC_CLRP 0x50524c43u /* 'CLRP' little-endian */

/* Parameter-profile identifier, not a wire-version / compatibility field. */
#define CHIPMUNK_LRS_PARAMS_C0 0x3043534cu /* 'LSC0' little-endian */

#define CHIPMUNK_LRS_SEED_BYTES 32u
#define CHIPMUNK_LRS_CHALLENGE_SEED_BYTES 32u
#define CHIPMUNK_LRS_K 6u
#define CHIPMUNK_LRS_CHALLENGE_WEIGHT 37u
#define CHIPMUNK_LRS_Q_BITS 22u
#define CHIPMUNK_LRS_POLY_QPACK_BYTES ((CHIPMUNK_N * CHIPMUNK_LRS_Q_BITS) / 8u)

#define CHIPMUNK_LRS_WITNESS_BOUND 13
#define CHIPMUNK_LRS_BETA (CHIPMUNK_LRS_CHALLENGE_WEIGHT * CHIPMUNK_LRS_WITNESS_BOUND)
#define CHIPMUNK_LRS_RESPONSE_BOUND 524288
#define CHIPMUNK_LRS_MASK_BOUND (CHIPMUNK_LRS_RESPONSE_BOUND + CHIPMUNK_LRS_BETA)
#define CHIPMUNK_LRS_MAX_ATTEMPTS 2048u

typedef struct chipmunk_lrs_public_key {
    uint32_t magic;
    uint32_t params_id;
    uint32_t reserved0;
    uint32_t reserved1;
    uint8_t pk_seed[CHIPMUNK_LRS_SEED_BYTES];
    uint8_t P[CHIPMUNK_LRS_POLY_QPACK_BYTES];
} chipmunk_lrs_public_key_t;

typedef struct chipmunk_lrs_secret_key {
    uint32_t magic;
    uint32_t params_id;
    uint32_t reserved0;
    uint32_t reserved1;
    uint8_t x_seed[CHIPMUNK_LRS_SEED_BYTES];
    uint8_t pk_seed[CHIPMUNK_LRS_SEED_BYTES];
    uint8_t P[CHIPMUNK_LRS_POLY_QPACK_BYTES];
} chipmunk_lrs_secret_key_t;

_Static_assert(CHIPMUNK_LRS_POLY_QPACK_BYTES == 1408u,
               "C0 q-packed polynomial size must stay pinned");
_Static_assert(sizeof(chipmunk_lrs_public_key_t) == 1456u,
               "CLPK canonical size drift");
_Static_assert(sizeof(chipmunk_lrs_secret_key_t) == 1488u,
               "CLSK canonical size drift");

#define CHIPMUNK_LRS_POP_HEADER_BYTES 96u
#define CHIPMUNK_LRS_POP_RESPONSE_BYTES \
        ((size_t)CHIPMUNK_LRS_K * (size_t)CHIPMUNK_LRS_POLY_QPACK_BYTES)
#define CHIPMUNK_LRS_POP_BYTES \
        (CHIPMUNK_LRS_POP_HEADER_BYTES + CHIPMUNK_LRS_POP_RESPONSE_BYTES)

_Static_assert(CHIPMUNK_LRS_POP_RESPONSE_BYTES == 8448u,
               "CLRP response area must equal K * qpack(poly)");
_Static_assert(CHIPMUNK_LRS_POP_BYTES == 8544u,
               "CLRP canonical wire size drift");

int chipmunk_lrs_poly_qpack(uint8_t a_out[CHIPMUNK_LRS_POLY_QPACK_BYTES],
                            const chipmunk_poly_t *a_poly);

int chipmunk_lrs_poly_qunpack(chipmunk_poly_t *a_poly,
                              const uint8_t a_in[CHIPMUNK_LRS_POLY_QPACK_BYTES]);

int chipmunk_lrs_poly_chknorm_centered(const chipmunk_poly_t *a_poly,
                                       int32_t a_bound);

int chipmunk_lrs_h_to_poly_q(chipmunk_poly_t *a_poly,
                             const char *a_domain,
                             uint32_t a_params_id,
                             const uint8_t *a_seed_material,
                             size_t a_seed_material_size,
                             uint32_t a_index);

int chipmunk_lrs_h_to_short_poly(chipmunk_poly_t *a_poly,
                                 const char *a_domain,
                                 uint32_t a_params_id,
                                 const uint8_t a_seed[CHIPMUNK_LRS_SEED_BYTES],
                                 uint32_t a_index,
                                 int32_t a_bound);

int chipmunk_lrs_h_to_sparse_ternary(chipmunk_poly_t *a_challenge,
                                     const char *a_domain,
                                     uint32_t a_params_id,
                                     const uint8_t a_seed[CHIPMUNK_LRS_CHALLENGE_SEED_BYTES]);

int chipmunk_lrs_derive_witness(chipmunk_poly_t a_x[CHIPMUNK_LRS_K],
                                const uint8_t a_x_seed[CHIPMUNK_LRS_SEED_BYTES]);

int chipmunk_lrs_derive_A_pk(chipmunk_poly_t a_A_pk[CHIPMUNK_LRS_K],
                             const uint8_t a_pk_seed[CHIPMUNK_LRS_SEED_BYTES]);

int chipmunk_lrs_derive_A_I(chipmunk_poly_t a_A_I[CHIPMUNK_LRS_K],
                            const uint8_t a_pk_seed[CHIPMUNK_LRS_SEED_BYTES],
                            const uint8_t a_P[CHIPMUNK_LRS_POLY_QPACK_BYTES]);

int chipmunk_lrs_relation_eval(chipmunk_poly_t *a_out,
                               const chipmunk_poly_t a_A[CHIPMUNK_LRS_K],
                               const chipmunk_poly_t a_x[CHIPMUNK_LRS_K]);

int chipmunk_lrs_keypair_from_seeds(chipmunk_lrs_public_key_t *a_pk,
                                    chipmunk_lrs_secret_key_t *a_sk,
                                    const uint8_t a_x_seed[CHIPMUNK_LRS_SEED_BYTES],
                                    const uint8_t a_pk_seed[CHIPMUNK_LRS_SEED_BYTES]);

int chipmunk_lrs_key_image(uint8_t a_key_image[CHIPMUNK_LRS_POLY_QPACK_BYTES],
                           const chipmunk_lrs_secret_key_t *a_sk);

int chipmunk_lrs_public_key_validate(const chipmunk_lrs_public_key_t *a_pk);

int chipmunk_lrs_secret_key_validate(const chipmunk_lrs_secret_key_t *a_sk);

int chipmunk_lrs_public_key_hash(uint8_t a_out[32],
                                 const chipmunk_lrs_public_key_t *a_pk);

int chipmunk_lrs_ring_hash(uint8_t a_out[32],
                           const chipmunk_lrs_public_key_t *a_public_keys,
                           size_t a_ring_size);

/*
 * PoP (CLRP) — proves knowledge of the LRS witness x for the public relation
 * P = Sum A_pk[j] * x[j].  Canonical wire is exactly CHIPMUNK_LRS_POP_BYTES.
 *
 * a_randomness_seed feeds the SHAKE256 mask sampler; the caller is
 * responsible for sourcing real CSPRNG entropy in production and a
 * test-only fixed seed in KATs.  pop_create restarts the full transcript
 * on rejection and returns -EAGAIN after CHIPMUNK_LRS_MAX_ATTEMPTS.
 */
int chipmunk_lrs_pop_create(uint8_t *a_pop,
                            size_t a_pop_size,
                            const chipmunk_lrs_secret_key_t *a_sk,
                            const uint8_t a_randomness_seed[CHIPMUNK_LRS_SEED_BYTES]);

int chipmunk_lrs_pop_verify(const uint8_t *a_pop,
                            size_t a_pop_size,
                            const chipmunk_lrs_public_key_t *a_pk);

#ifdef __cplusplus
}
#endif
