/*
 * dap_enc_chipmunk_ring_governance.h — Cellframe governance integration
 * surface (CR-9.6).  Thin dap_enc_key wrappers over CR-9.4.A threshold
 * dealer/combiner and CR-9.5 Proof-of-Possession.
 *
 * See SLC `documentation_831b3c2fd035cada` (CR-9.6 design) and
 * SLC `documentation_c13915924bce1940` (Cellframe integration guide).
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "dap_enc_key.h"
#include "dap_chipmunk_ring_threshold.h"

/* chipmunk_ring_container_t — defined in chipmunk_ring.h (internal path). */
struct chipmunk_ring_container;
typedef struct chipmunk_ring_container chipmunk_ring_container_t;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wire size of a CR-9.5/CR-11.E PoP blob (8-byte header + ht signature).
 *        Use this to size registry buffers in Cellframe cert storage.
 */
size_t dap_enc_chipmunk_ring_pop_wire_size(void);

/**
 * @brief Split a 32-byte governance master seed into n Shamir shares
 *        with reconstruction threshold t (CR-9.4.A).
 *
 * Thin wrapper over chipmunk_ring_threshold_deal.
 */
int dap_enc_chipmunk_ring_governance_deal(const uint8_t a_master_seed[32],
                                          uint32_t a_n,
                                          uint32_t a_t,
                                          chipmunk_ring_threshold_share_t *a_out_shares);

/**
 * @brief Reconstruct master seed from t shares and materialise a
 *        ChipmunkRing @c dap_enc_key (pub + priv hypertree layout).
 *
 * On success @a *a_out_key is a newly allocated key with type
 * @c DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING.  Caller must release via
 * @c dap_enc_chipmunk_ring_key_delete (or the enc-key vtable).
 *
 * @return 0 on success; negative errno-style codes on failure.
 *         On failure @a *a_out_key is NULL.
 */
int dap_enc_chipmunk_ring_governance_combine_to_key(
    const chipmunk_ring_threshold_share_t *a_shares,
    uint32_t a_t,
    struct dap_enc_key **a_out_key);

/**
 * @brief Create a Proof-of-Possession for a ring member key (CR-11.E v2).
 *
 * @a a_member_key MUST carry private key material.  PoP uses the
 * Merkle-committed reserved PoP leaf and does not mutate the production
 * @c leaf_index counter.
 */
int dap_enc_chipmunk_ring_member_pop_create(struct dap_enc_key *a_member_key,
                                            uint8_t *a_out_pop,
                                            size_t a_out_pop_size);

/**
 * @brief Verify a PoP against a member's public key material only.
 */
int dap_enc_chipmunk_ring_member_pop_verify(struct dap_enc_key *a_member_key,
                                            const uint8_t *a_pop,
                                            size_t a_pop_size);

/**
 * @brief Create a ring container only if every participant's PoP
 *        verifies under their published public key (fail-closed).
 *
 * @a a_member_keys   array of @a a_ring_size public @c dap_enc_key
 *                    entries (priv_key_data ignored; may be NULL).
 * @a a_member_pops   parallel array of PoP blobs, each exactly
 *                    dap_enc_chipmunk_ring_pop_wire_size() bytes.
 */
int dap_enc_chipmunk_ring_container_create_with_pop(
    struct dap_enc_key **a_member_keys,
    const uint8_t *const *a_member_pops,
    size_t a_ring_size,
    chipmunk_ring_container_t *a_out_ring);

#ifdef __cplusplus
}
#endif
