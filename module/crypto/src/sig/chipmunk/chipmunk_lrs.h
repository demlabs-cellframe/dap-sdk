/*
 * Chipmunk LRS — native linkable ring signature on the Chipmunk lattice
 * substrate.  One parameter set, one wire family.
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

/*
 * Response-polynomial packing (z-pack).
 *
 * After rejection sampling every coefficient z_i of a response polynomial
 * satisfies  -(RESPONSE_BOUND) <= z_i <= (RESPONSE_BOUND-1).
 * That gives exactly 2*RESPONSE_BOUND = 2^20 = 1 048 576 distinct values,
 * which fit in exactly 20 bits.  The biased representation is:
 *
 *   stored = z_i + RESPONSE_BOUND   ∈ [0, 2*RESPONSE_BOUND - 1]
 *
 * This is 2 bits tighter than the 22-bit full q-range pack (POLY_QPACK_BYTES)
 * and should only be used for response (z_pk / z_T) sections — never for
 * ring elements like link tags or public keys that span the full [0, q-1]
 * range.
 *
 * Note: the rejection check in sign loops must be changed to the
 * HALF-OPEN interval [-RESPONSE_BOUND, RESPONSE_BOUND) so that the biased
 * value never exceeds 2*RESPONSE_BOUND - 1.
 */
#define CHIPMUNK_LRS_Z_BITS         20u
#define CHIPMUNK_LRS_Z_BIAS         ((int32_t)CHIPMUNK_LRS_RESPONSE_BOUND)
#define CHIPMUNK_LRS_POLY_ZPACK_BYTES \
        ((CHIPMUNK_N * CHIPMUNK_LRS_Z_BITS) / 8u)

_Static_assert(CHIPMUNK_LRS_POLY_ZPACK_BYTES == 1280u,
               "z-packed polynomial size must be 1280 bytes (20 bits × 512 / 8)");

typedef struct chipmunk_lrs_public_key {
    uint32_t magic;
    uint32_t params_id;
    uint32_t reserved0;
    uint32_t reserved1;
    uint8_t P[CHIPMUNK_LRS_POLY_QPACK_BYTES];
} chipmunk_lrs_public_key_t;

typedef struct chipmunk_lrs_secret_key {
    uint32_t magic;
    uint32_t params_id;
    uint32_t reserved0;
    uint32_t reserved1;
    uint8_t x_seed[CHIPMUNK_LRS_SEED_BYTES];
    uint8_t P[CHIPMUNK_LRS_POLY_QPACK_BYTES];
} chipmunk_lrs_secret_key_t;

_Static_assert(CHIPMUNK_LRS_POLY_QPACK_BYTES == 1408u,
               "q-packed polynomial size must stay pinned");
_Static_assert(sizeof(chipmunk_lrs_public_key_t) == 1424u,
               "CLPK size drift");
_Static_assert(sizeof(chipmunk_lrs_secret_key_t) == 1456u,
               "CLSK size drift");

#define CHIPMUNK_LRS_POP_HEADER_BYTES 96u
#define CHIPMUNK_LRS_POP_RESPONSE_BYTES \
        ((size_t)CHIPMUNK_LRS_K * (size_t)CHIPMUNK_LRS_POLY_QPACK_BYTES)
#define CHIPMUNK_LRS_POP_BYTES \
        (CHIPMUNK_LRS_POP_HEADER_BYTES + CHIPMUNK_LRS_POP_RESPONSE_BYTES)

_Static_assert(CHIPMUNK_LRS_POP_RESPONSE_BYTES == 8448u,
               "CLRP response area must equal K * qpack(poly)");
_Static_assert(CHIPMUNK_LRS_POP_BYTES == 8544u,
               "CLRP canonical wire size drift");

#define CHIPMUNK_LRS_RING_MIN 2u
#define CHIPMUNK_LRS_RING_MAX 64u

#define CHIPMUNK_LRS_SIG_HEADER_BYTES 1504u
#define CHIPMUNK_LRS_SIG_PER_MEMBER_BYTES \
        ((size_t)CHIPMUNK_LRS_K * (size_t)CHIPMUNK_LRS_POLY_QPACK_BYTES)

_Static_assert(CHIPMUNK_LRS_SIG_HEADER_BYTES ==
               8u * 4u + 32u + (size_t)CHIPMUNK_LRS_POLY_QPACK_BYTES + 32u,
               "CLRS canonical header size drift");
_Static_assert(CHIPMUNK_LRS_SIG_PER_MEMBER_BYTES == 8448u,
               "CLRS per-member response size drift");

int chipmunk_lrs_poly_qpack(uint8_t a_out[CHIPMUNK_LRS_POLY_QPACK_BYTES],
                            const chipmunk_poly_t *a_poly,
                            uint64_t q);

int chipmunk_lrs_poly_qunpack(chipmunk_poly_t *a_poly,
                              const uint8_t a_in[CHIPMUNK_LRS_POLY_QPACK_BYTES],
                              uint64_t q);

/*
 * z-pack / z-unpack: 20-bit biased encoding for response polynomials.
 *
 * zpack: accepts coefficients in [-RESPONSE_BOUND, RESPONSE_BOUND-1] (half-
 *        open, so that biased = coeff + RESPONSE_BOUND fits in 20 bits).
 *        Returns -EINVAL if any coefficient is out of range.
 *
 * zunpack: decodes 20-bit biased values back to signed coefficients in
 *          [-RESPONSE_BOUND, RESPONSE_BOUND-1].  Returns -EINVAL if a stored
 *          value exceeds 2*RESPONSE_BOUND-1.
 */
int chipmunk_lrs_poly_zpack(uint8_t a_out[CHIPMUNK_LRS_POLY_ZPACK_BYTES],
                             const chipmunk_poly_t *a_poly);

int chipmunk_lrs_poly_zunpack(chipmunk_poly_t *a_poly,
                               const uint8_t a_in[CHIPMUNK_LRS_POLY_ZPACK_BYTES]);

int chipmunk_lrs_poly_chknorm_centered(const chipmunk_poly_t *a_poly,
                                       int32_t a_bound,
                                       uint64_t q);

int chipmunk_lrs_h_to_poly_q(chipmunk_poly_t *a_poly,
                             const char *a_domain,
                             uint32_t a_params_id,
                             const uint8_t *a_seed_material,
                             size_t a_seed_material_size,
                             uint32_t a_index,
                             uint64_t q);

int chipmunk_lrs_h_to_short_poly(chipmunk_poly_t *a_poly,
                                 const char *a_domain,
                                 uint32_t a_params_id,
                                 const uint8_t a_seed[CHIPMUNK_LRS_SEED_BYTES],
                                 uint32_t a_index,
                                 int32_t a_bound);

/*
 * General-purpose bounded polynomial sampler: produces coefficients in
 * [-a_bound, +a_bound] via rejection sampling over SHAKE256.  Unlike
 * chipmunk_lrs_h_to_short_poly, a_bound may be as large as q/2 - 1 (covers
 * MASK_BOUND = 524769).  Used by CLTS for pk and tag mask sampling.
 */
int chipmunk_lrs_h_to_bounded_poly(chipmunk_poly_t *a_poly,
                                   const char *a_domain,
                                   uint32_t a_params_id,
                                   const uint8_t *a_seed_material,
                                   size_t a_seed_material_size,
                                   uint32_t a_index,
                                   int32_t a_bound);

int chipmunk_lrs_h_to_sparse_ternary(chipmunk_poly_t *a_challenge,
                                     const char *a_domain,
                                     uint32_t a_params_id,
                                     const uint8_t a_seed[CHIPMUNK_LRS_CHALLENGE_SEED_BYTES]);

int chipmunk_lrs_derive_witness(chipmunk_poly_t a_x[CHIPMUNK_LRS_K],
                                const uint8_t a_x_seed[CHIPMUNK_LRS_SEED_BYTES]);

/*
 * System-wide public-key matrix A_pk (CR-11.G Phase 7.1).
 * Derived from params_id only — identical for every ring member.
 */
int chipmunk_lrs_derive_A_pk(chipmunk_poly_t a_A_pk[CHIPMUNK_LRS_K],
                             uint32_t a_params_id);

int chipmunk_lrs_derive_A_I(chipmunk_poly_t a_A_I[CHIPMUNK_LRS_K],
                            uint32_t a_params_id,
                            const uint8_t a_P[CHIPMUNK_LRS_POLY_QPACK_BYTES]);

int chipmunk_lrs_relation_eval(chipmunk_poly_t *a_out,
                               const chipmunk_poly_t a_A[CHIPMUNK_LRS_K],
                               const chipmunk_poly_t a_x[CHIPMUNK_LRS_K],
                               uint64_t q);

int chipmunk_lrs_keypair_from_seeds(chipmunk_lrs_public_key_t *a_pk,
                                    chipmunk_lrs_secret_key_t *a_sk,
                                    const uint8_t a_x_seed[CHIPMUNK_LRS_SEED_BYTES]);

int chipmunk_lrs_key_image(uint8_t a_key_image[CHIPMUNK_LRS_POLY_QPACK_BYTES],
                           const chipmunk_lrs_secret_key_t *a_sk);

/*
 * Phase 6-full: Stealth address derivation.
 *
 * Sender creates ephemeral keypair and derives one-time public key:
 *   1. Generate ephemeral seed e (32 bytes CSPRNG)
 *   2. ephemeral_sk = derive_witness(e), ephemeral_pk = A · ephemeral_sk
 *   3. shared = SHA3-256(recipient_pk || ephemeral_pk)
 *   4. derived_sk = scan_sk + shared (modular offset on seed)
 *   5. derived_pk = A · derived_sk
 *   6. Output goes to derived_pk, ephemeral_pk published in OUT_ANON
 *
 * Recipient scans:
 *   1. For each OUT_ANON with ephemeral_pk ≠ 0:
 *   2. shared = SHA3-256(my_pk || ephemeral_pk)
 *   3. derived_sk = my_sk + shared
 *   4. derived_pk = A · derived_sk
 *   5. If derived_pk == addr → this output belongs to me
 *
 * This provides unlinkability: each output has a unique one-time key
 * that cannot be linked to the recipient's public address.
 */

/*
 * Derive one-time secret key from scan secret + shared secret.
 * a_scan_sk: recipient's base secret key (x_seed)
 * a_shared: 32-byte shared secret = H(recipient_pk || ephemeral_pk)
 * a_out_sk: derived one-time secret key (new x_seed)
 * Returns 0 on success.
 */
int chipmunk_lrs_stealth_derive_sk(uint8_t a_out_sk[CHIPMUNK_LRS_SEED_BYTES],
                                     const uint8_t a_scan_sk[CHIPMUNK_LRS_SEED_BYTES],
                                     const uint8_t a_shared[32]);

/*
 * Derive one-time public key from scan public key + shared secret.
 * a_scan_pk: recipient's base public key
 * a_shared: 32-byte shared secret
 * a_out_pk: derived one-time public key
 * Returns 0 on success.
 */
int chipmunk_lrs_stealth_derive_pk(chipmunk_lrs_public_key_t *a_out_pk,
                                     const chipmunk_lrs_public_key_t *a_scan_pk,
                                     const uint8_t a_shared[32]);

/*
 * Compute shared secret = H(recipient_pk || ephemeral_pk).
 * Uses SHA3-256 for domain separation and collision resistance.
 */
void chipmunk_lrs_stealth_shared_secret(uint8_t a_out[32],
                                          const chipmunk_lrs_public_key_t *a_recipient_pk,
                                          const chipmunk_lrs_public_key_t *a_ephemeral_pk);

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
                            const uint8_t a_randomness_seed[CHIPMUNK_LRS_SEED_BYTES],
                            uint64_t q);

int chipmunk_lrs_pop_verify(const uint8_t *a_pop,
                            size_t a_pop_size,
                            const chipmunk_lrs_public_key_t *a_pk,
                            uint64_t q);

/*
 * Canonical CLRS signature size for a given ring size.  Returns 0 if the
 * ring size falls outside [CHIPMUNK_LRS_RING_MIN, CHIPMUNK_LRS_RING_MAX].
 */
size_t chipmunk_lrs_signature_size(uint32_t a_ring_size);

/*
 * Sign a_message under the given canonical sorted ring with a_sk's witness.
 * a_ring may be passed in any order; the implementation canonical-sorts it
 * and rejects rings that do not contain the signer's CLPK or contain
 * duplicates.  a_randomness_seed feeds the SHAKE256 mask/simulation samplers.
 * The output buffer must be exactly chipmunk_lrs_signature_size(ring_size).
 * Returns -EAGAIN if the rejection loop is exhausted.
 */
int chipmunk_lrs_sign(uint8_t *a_sig,
                      size_t a_sig_size,
                      const chipmunk_lrs_secret_key_t *a_sk,
                      const chipmunk_lrs_public_key_t *a_ring,
                      size_t a_ring_size,
                      const uint8_t *a_message,
                      size_t a_message_size,
                      const uint8_t a_randomness_seed[CHIPMUNK_LRS_SEED_BYTES],
                      uint64_t q);

/*
 * Verify a_sig over a_message against the candidate ring.  Wire and
 * algebra gates fail closed.  ring may be passed in any order; the
 * implementation canonical-sorts it and re-derives the canonical ring
 * hash before any algebraic work.
 */
int chipmunk_lrs_verify(const uint8_t *a_sig,
                        size_t a_sig_size,
                        const chipmunk_lrs_public_key_t *a_ring,
                        size_t a_ring_size,
                        const uint8_t *a_message,
                        size_t a_message_size,
                        uint64_t q);

#ifdef __cplusplus
}
#endif
