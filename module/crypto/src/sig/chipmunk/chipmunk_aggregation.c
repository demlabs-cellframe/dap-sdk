#include "chipmunk_aggregation.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"
#include "chipmunk_hash.h"
#include "chipmunk_hots.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "dap_common.h"
#include "dap_hash.h"
#include "dap_hash_sha3.h"
#include "dap_rand.h"

#define LOG_TAG "chipmunk_aggregation"

// Если OpenSSL недоступен, определяем константу
#ifndef SHA256_DIGEST_LENGTH
#define SHA256_DIGEST_LENGTH 32
#endif

static bool s_debug_more = false;

// Вспомогательная функция для редукции коэффициента по модулю q
static inline int32_t chipmunk_poly_reduce_coeff(int32_t coeff) {
    int32_t t = coeff % CHIPMUNK_Q;
    if (t > CHIPMUNK_Q_OVER_TWO) {
        t -= CHIPMUNK_Q;
    } else if (t < -CHIPMUNK_Q_OVER_TWO) {
        t += CHIPMUNK_Q;
    }
    return t;
}

/**
 * @brief Lift coefficient into canonical [0, q) range.
 *
 * CR-D6/D7 helper: the NTT-domain arithmetic uses the canonical
 * non-negative representation, so every coefficient that enters
 * chipmunk_poly_mul_ntt / chipmunk_poly_add_ntt / chipmunk_poly_equal must be
 * reduced with this helper first.  Relying on implicit truncation of a signed
 * modulo is what produced the ±Q/2 sign flips earlier audits tagged as
 * CR-D16.
 */
static inline int32_t s_canonicalize_mod_q(int32_t a_coeff) {
    int32_t l_t = a_coeff % CHIPMUNK_Q;
    if (l_t < 0) {
        l_t += CHIPMUNK_Q;
    }
    return l_t;
}

/**
 * @brief Expand a ternary randomizer into a full chipmunk_poly_t in NTT domain.
 *
 * CR-D6/D7 fix (Round-4): the original aggregation applied the ternary
 * randomizer as a coefficient-wise Hadamard product, which is NOT the
 * polynomial multiplication that appears in the HOTS aggregate identity
 * `Σ_i A_i · (Σ_j r_j · σ_ji) = H(m) · Σ_j r_j · v0_j + Σ_j r_j · v1_j`.
 * Proper application requires lifting the int8 ternary coefficients to
 * int32_t, running the forward NTT to move into the frequency domain, and
 * then using chipmunk_poly_mul_ntt (pointwise Montgomery-reduced mul).
 */
static void s_randomizer_to_poly_ntt(const chipmunk_randomizer_t *a_randomizer,
                                     chipmunk_poly_t *a_ntt_out) {
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int32_t l_coeff = (int32_t)a_randomizer->coeffs[i];
        a_ntt_out->coeffs[i] = s_canonicalize_mod_q(l_coeff);
    }
    chipmunk_ntt(a_ntt_out->coeffs);
}

/**
 * @brief Verify that every signer's HVC leaf digest matches the full HOTS pk.
 *
 * CR-D6/D7 helper: the Merkle-tree leaf `public_key_roots[j]` is a SHA3-
 * derived HVC polynomial computed from `hots_pks[j]`.  Without this pinning
 * check an adversary could ship a multi-signature with a tampered `hots_pks`
 * array (different v0, v1) while keeping the original `public_key_roots`, so
 * the Merkle root would still verify but the aggregate identity would use
 * attacker-controlled HOTS pks.  Recompute the digest and compare constant-
 * time-ish (memcmp on a fixed-size struct is acceptable because the pks are
 * public values; timing leakage on public data is not a threat).
 */
// Wrap a HOTS pk (v0, v1) into a transient chipmunk_public_key_t so it can
// be fed to chipmunk_hots_pk_to_hvc_poly without relying on layout punning.
// rho_seed is zeroed because chipmunk_hots_pk_to_hvc_poly does not read it.
static void s_hots_pk_to_full_pk(const chipmunk_hots_public_key_t *a_hots_pk,
                                 chipmunk_public_key_t *a_full_pk_out) {
    memset(a_full_pk_out->rho_seed, 0, sizeof(a_full_pk_out->rho_seed));
    memcpy(&a_full_pk_out->v0, &a_hots_pk->v0, sizeof(chipmunk_poly_t));
    memcpy(&a_full_pk_out->v1, &a_hots_pk->v1, sizeof(chipmunk_poly_t));
}

static bool s_verify_pk_leaf_binding(const chipmunk_hots_public_key_t *a_hots_pk,
                                     const chipmunk_hvc_poly_t *a_expected_leaf) {
    chipmunk_public_key_t l_full_pk;
    s_hots_pk_to_full_pk(a_hots_pk, &l_full_pk);

    chipmunk_hvc_poly_t l_recomputed;
    memset(&l_recomputed, 0, sizeof(l_recomputed));
    int l_rc = chipmunk_hots_pk_to_hvc_poly(&l_full_pk, &l_recomputed);
    if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to recompute HVC leaf for binding check: %d", l_rc);
        return false;
    }
    return memcmp(&l_recomputed, a_expected_leaf, sizeof(l_recomputed)) == 0;
}

// === Randomizer Functions ===

/**
 * Generate randomizers from public key roots
 * Based on SHA256 hash of concatenated roots
 */
int chipmunk_randomizers_from_pks(const chipmunk_hvc_poly_t *roots, 
                                  size_t count,
                                  chipmunk_randomizers_t *randomizers) {
    if (!roots || !randomizers || count == 0) {
        return -1;
    }

    // Allocate randomizers
    randomizers->randomizers = calloc(count, sizeof(chipmunk_randomizer_t));
    if (!randomizers->randomizers) {
        return -1;
    }
    randomizers->count = count;
    
    // Concatenate all roots into single buffer
    size_t input_size = count * sizeof(chipmunk_hvc_poly_t);
    uint8_t *hash_input = malloc(input_size + sizeof(uint32_t));
    if (!hash_input) {
        free(randomizers->randomizers);
        randomizers->randomizers = NULL;
        return -1;
    }
    
    memcpy(hash_input, roots, input_size);
    
    // Generate randomizers using dap_hash_sha3_256 expansion
    for (size_t i = 0; i < count; i++) {
        // Add counter to input for domain separation
        uint32_t counter = (uint32_t)i;
        memcpy(hash_input + input_size, &counter, sizeof(counter));
        
        // Hash to get random bits
        dap_hash_sha3_256_t hash;
        dap_hash_sha3_256(hash_input, input_size + sizeof(uint32_t), &hash);
        
        // Convert hash bits to ternary coefficients
        for (size_t j = 0; j < CHIPMUNK_N && j < DAP_HASH_SHA3_256_SIZE * 4; j++) {
            uint8_t byte_idx = (j * 2) % DAP_HASH_SHA3_256_SIZE;
            uint8_t bit_idx = (j * 2) / DAP_HASH_SHA3_256_SIZE;
            
            // Use 2 bits per coefficient to get 4 values, map to {-1, 0, 1}
            uint8_t bits = (hash.raw[byte_idx] >> (bit_idx * 2)) & 0x03;
            
            switch (bits) {
                case 0: randomizers->randomizers[i].coeffs[j] = 0; break;
                case 1: randomizers->randomizers[i].coeffs[j] = 1; break;
                case 2: randomizers->randomizers[i].coeffs[j] = -1; break;
                case 3: randomizers->randomizers[i].coeffs[j] = 0; break; // Map 3 to 0 for balance
            }
        }
    }

    free(hash_input);
    return 0;
}

/**
 * Generate random randomizers for testing/non-deterministic use
 */
int chipmunk_randomizers_generate_random(size_t count, 
                                         chipmunk_randomizers_t *randomizers) {
    if (!randomizers || count == 0) {
        return -1;
    }

    randomizers->randomizers = calloc(count, sizeof(chipmunk_randomizer_t));
    if (!randomizers->randomizers) {
        return -2;
    }
    randomizers->count = count;

    // Use rejection sampling on top of CSPRNG to produce an unbiased uniform
    // ternary distribution {-1, 0, 1}. rand()%3 is biased (since RAND_MAX is
    // not a multiple of 3) and uses a non-cryptographic PRNG. Randomizers are
    // consumed inside the batch-verification / aggregation pipeline where a
    // biased or predictable distribution weakens soundness.
    for (size_t i = 0; i < count; i++) {
        size_t filled = 0;
        while (filled < (size_t)CHIPMUNK_N) {
            uint8_t buf[64];
            if (dap_random_bytes(buf, sizeof(buf)) != 0) {
                free(randomizers->randomizers);
                randomizers->randomizers = NULL;
                randomizers->count = 0;
                return -3;
            }
            for (size_t b = 0; b < sizeof(buf) && filled < (size_t)CHIPMUNK_N; b++) {
                uint8_t v = buf[b];
                // Accept only values < 252 (84*3) to avoid modulo bias.
                if (v >= 252) {
                    continue;
                }
                uint8_t r = (uint8_t)(v % 3u);
                randomizers->randomizers[i].coeffs[filled++] = (int8_t)((int)r - 1);
            }
        }
    }

    return 0;
}

/**
 * Free randomizers structure
 */
void chipmunk_randomizers_free(chipmunk_randomizers_t *randomizers) {
    if (randomizers && randomizers->randomizers) {
        free(randomizers->randomizers);
        randomizers->randomizers = NULL;
        randomizers->count = 0;
    }
}

// === HOTS Signature Aggregation ===

/**
 * @brief Apply randomizer r to signature sigma as full polynomial multiplication.
 *
 * CR-D6/D7 fix (Round-4): the HOTS aggregate identity expands to
 * `Σ_i A_i · (Σ_j r_j · σ_ji) = H(m) · Σ_j r_j · v0_j + Σ_j r_j · v1_j`.
 * Here `r_j · σ_ji` means polynomial multiplication in Rq, NOT coefficient-
 * wise Hadamard product.  Concretely we:
 *   1. lift the ternary randomizer into a chipmunk_poly_t and NTT it,
 *   2. NTT every HOTS-sigma polynomial (signatures are stored in time domain
 *      per chipmunk_hots_sign's inv-NTT at the end),
 *   3. pointwise-multiply in NTT domain,
 *   4. inv-NTT back to time domain so the wire format matches stand-alone
 *      HOTS signatures.
 *
 * Callers MUST NOT rely on the old coefficient-wise semantics; the new output
 * is materially different and only verifies under chipmunk_verify_multi_
 * signature's matching NTT-domain aggregation.
 */
int chipmunk_hots_sig_randomize(const chipmunk_hots_signature_t *sig,
                                const chipmunk_randomizer_t *randomizer,
                                chipmunk_hots_signature_t *randomized_sig) {
    if (!sig || !randomizer || !randomized_sig) {
        return -1;
    }

    chipmunk_poly_t l_r_ntt;
    s_randomizer_to_poly_ntt(randomizer, &l_r_ntt);

    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        // Bring sigma[i] into NTT domain.
        chipmunk_poly_t l_sigma_ntt = sig->sigma[i];
        for (int j = 0; j < CHIPMUNK_N; j++) {
            l_sigma_ntt.coeffs[j] = s_canonicalize_mod_q(l_sigma_ntt.coeffs[j]);
        }
        chipmunk_ntt(l_sigma_ntt.coeffs);

        // Pointwise multiply in NTT domain.
        chipmunk_poly_t l_prod_ntt;
        chipmunk_poly_mul_ntt(&l_prod_ntt, &l_r_ntt, &l_sigma_ntt);

        // Back to time domain.
        chipmunk_invntt(l_prod_ntt.coeffs);
        for (int j = 0; j < CHIPMUNK_N; j++) {
            randomized_sig->sigma[i].coeffs[j] = s_canonicalize_mod_q(l_prod_ntt.coeffs[j]);
        }
    }

    return 0;
}

/**
 * @brief Aggregate randomized HOTS signatures across all signers.
 *
 * CR-D6/D7 fix (Round-4): aggregation is performed fully in the NTT domain
 * to preserve the ring structure.  For each polynomial index i ∈ [0, GAMMA)
 * we accumulate
 *     aggregated[i] ← Σ_j  NTT(r_j) ⊙ NTT(σ_ji)     (mod q, pointwise)
 * and only convert the final sum back to the time domain.  Using the earlier
 * "sum coefficient-wise after randomizing in time domain" path gave a result
 * that did not satisfy any linear equation verifiable against the HOTS pks,
 * so the verifier had to resort to a ±10 % slack tolerance and accepted
 * essentially any non-zero blob (CR-D6).
 */
int chipmunk_hots_aggregate_with_randomizers(const chipmunk_hots_signature_t *signatures,
                                             const chipmunk_randomizer_t *randomizers,
                                             size_t count,
                                             chipmunk_aggregated_hots_sig_t *aggregated) {
    if (!signatures || !randomizers || !aggregated || count == 0) {
        return -1;
    }

    memset(aggregated, 0, sizeof(chipmunk_aggregated_hots_sig_t));
    aggregated->is_randomized = true;

    chipmunk_poly_t l_accum_ntt[CHIPMUNK_GAMMA];
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        memset(&l_accum_ntt[i], 0, sizeof(chipmunk_poly_t));
    }

    for (size_t j = 0; j < count; j++) {
        chipmunk_poly_t l_r_ntt;
        s_randomizer_to_poly_ntt(&randomizers[j], &l_r_ntt);

        for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
            chipmunk_poly_t l_sigma_ntt = signatures[j].sigma[i];
            for (int k = 0; k < CHIPMUNK_N; k++) {
                l_sigma_ntt.coeffs[k] = s_canonicalize_mod_q(l_sigma_ntt.coeffs[k]);
            }
            chipmunk_ntt(l_sigma_ntt.coeffs);

            chipmunk_poly_t l_prod_ntt;
            chipmunk_poly_mul_ntt(&l_prod_ntt, &l_r_ntt, &l_sigma_ntt);
            chipmunk_poly_add_ntt(&l_accum_ntt[i], &l_accum_ntt[i], &l_prod_ntt);
        }
    }

    // Store aggregated σ in time domain to match the single-HOTS wire format.
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        chipmunk_poly_t l_time = l_accum_ntt[i];
        chipmunk_invntt(l_time.coeffs);
        for (int k = 0; k < CHIPMUNK_N; k++) {
            aggregated->sigma[i].coeffs[k] = s_canonicalize_mod_q(l_time.coeffs[k]);
        }
    }

    return 0;
}

// === Multi-Signature Functions ===

/**
 * Create individual signature with Merkle proof.
 *
 * CR-D6/D7 (Round-4): `a_rho_seed` is the 32-byte seed that yields the public
 * HOTS matrix A via dap_chipmunk_hash_sample_matrix().  The caller MUST pass
 * the same rho_seed that was used to generate (public_key, secret_key) —
 * otherwise the aggregate verification will reject the signature.
 */
int chipmunk_create_individual_signature(const uint8_t *message,
                                         size_t message_len,
                                         const chipmunk_hots_secret_key_t *secret_key,
                                         const chipmunk_hots_public_key_t *public_key,
                                         const uint8_t a_rho_seed[32],
                                         const chipmunk_tree_t *tree,
                                         uint32_t leaf_index,
                                         chipmunk_individual_sig_t *individual_sig) {
    if (!message || !secret_key || !public_key || !a_rho_seed || !tree || !individual_sig) {
        return -1;
    }

    // Validate leaf index against actual tree size
    if (leaf_index >= tree->leaf_count) {
        return -2;
    }

    // Generate HOTS signature
    debug_if(s_debug_more, L_DEBUG, "Generating HOTS signature for leaf index %u", leaf_index);
    int ret = chipmunk_hots_sign((const chipmunk_hots_sk_t*)secret_key, message, message_len, &individual_sig->hots_sig);
    if (ret != 0) {
        return ret;
    }

    // Copy public key
    memcpy(&individual_sig->hots_pk, public_key, sizeof(chipmunk_hots_pk_t));
    memcpy(individual_sig->rho_seed, a_rho_seed, 32);

    // Generate Merkle proof
    ret = chipmunk_tree_gen_proof(tree, leaf_index, &individual_sig->proof);
    if (ret != 0) {
        return ret;
    }

    individual_sig->leaf_index = leaf_index;

    return 0;
}

/**
 * Aggregate multiple individual signatures into multi-signature
 */
int chipmunk_aggregate_signatures(const chipmunk_individual_sig_t *individual_sigs,
                                  size_t count,
                                  const uint8_t *message,
                                  size_t message_len,
                                  chipmunk_multi_signature_t *multi_sig) {
    if (!individual_sigs || !message || !multi_sig || count == 0) {
        return -1;
    }

    // CR-D6/D7 fix (Round-4): allocate the full HOTS pk array alongside the
    // lossy HVC-leaf projection.  Without hots_pks the verifier cannot
    // reconstruct the aggregate RHS and must accept near-arbitrary blobs.
    multi_sig->public_key_roots = calloc(count, sizeof(chipmunk_hvc_poly_t));
    multi_sig->hots_pks         = calloc(count, sizeof(chipmunk_hots_public_key_t));
    multi_sig->rho_seeds        = calloc(count, 32);
    multi_sig->proofs           = calloc(count, sizeof(chipmunk_path_t));
    multi_sig->leaf_indices     = calloc(count, sizeof(uint32_t));

    if (!multi_sig->public_key_roots || !multi_sig->hots_pks ||
        !multi_sig->rho_seeds || !multi_sig->proofs || !multi_sig->leaf_indices) {
        chipmunk_multi_signature_free(multi_sig);
        return -2;
    }

    multi_sig->signer_count = count;

    // Hash the message
    dap_hash_sha3_256_t message_hash;
    dap_hash_sha3_256(message, message_len, &message_hash);
    memcpy(multi_sig->message_hash, message_hash.raw, DAP_HASH_SHA3_256_SIZE);

    // Extract HOTS signatures and create randomizers
    chipmunk_hots_signature_t *hots_sigs = calloc(count, sizeof(chipmunk_hots_signature_t));
    if (!hots_sigs) {
        chipmunk_multi_signature_free(multi_sig);
        return -2;
    }

    // Collect per-signer material.
    for (size_t i = 0; i < count; i++) {
        memcpy(&hots_sigs[i], &individual_sigs[i].hots_sig, sizeof(chipmunk_hots_signature_t));

        memcpy(&multi_sig->proofs[i], &individual_sigs[i].proof, sizeof(chipmunk_path_t));
        multi_sig->leaf_indices[i] = individual_sigs[i].leaf_index;

        // Full HOTS pk — required by the aggregate verify equation.
        memcpy(&multi_sig->hots_pks[i], &individual_sigs[i].hots_pk,
               sizeof(chipmunk_hots_public_key_t));

        // rho_seed — per-signer public matrix seed; verifier re-derives A from it.
        memcpy(multi_sig->rho_seeds[i], individual_sigs[i].rho_seed, 32);

        // Tree-leaf digest (HVC-projected).  Pinned against hots_pks[i] at
        // verify time via s_verify_pk_leaf_binding().
        chipmunk_public_key_t l_full_pk;
        s_hots_pk_to_full_pk(&individual_sigs[i].hots_pk, &l_full_pk);
        int l_rc_leaf = chipmunk_hots_pk_to_hvc_poly(&l_full_pk,
                                                     &multi_sig->public_key_roots[i]);
        if (l_rc_leaf != CHIPMUNK_ERROR_SUCCESS) {
            free(hots_sigs);
            chipmunk_multi_signature_free(multi_sig);
            return l_rc_leaf;
        }
    }

    // Generate randomizers from public key roots
    chipmunk_randomizers_t randomizers;
    int ret = chipmunk_randomizers_from_pks(multi_sig->public_key_roots, count, &randomizers);
    if (ret != 0) {
        free(hots_sigs);
        chipmunk_multi_signature_free(multi_sig);
        return ret;
    }

    // Aggregate HOTS signatures with randomizers
    ret = chipmunk_hots_aggregate_with_randomizers(hots_sigs, randomizers.randomizers,
                                                   count, &multi_sig->aggregated_hots);

    free(hots_sigs);
    chipmunk_randomizers_free(&randomizers);

    if (ret != 0) {
        chipmunk_multi_signature_free(multi_sig);
        return ret;
    }

    return 0;
}

/**
 * Aggregate multiple individual signatures into multi-signature with tree
 */
int chipmunk_aggregate_signatures_with_tree(const chipmunk_individual_sig_t *individual_sigs,
                                            size_t count,
                                            const uint8_t *message,
                                            size_t message_len,
                                            const chipmunk_tree_t *tree,
                                            chipmunk_multi_signature_t *multi_sig) {
    if (!individual_sigs || !message || !multi_sig || !tree || count == 0) {
        return -1;
    }

    multi_sig->public_key_roots = calloc(count, sizeof(chipmunk_hvc_poly_t));
    multi_sig->hots_pks         = calloc(count, sizeof(chipmunk_hots_public_key_t));
    multi_sig->rho_seeds        = calloc(count, 32);
    multi_sig->proofs           = calloc(count, sizeof(chipmunk_path_t));
    multi_sig->leaf_indices     = calloc(count, sizeof(uint32_t));

    if (!multi_sig->public_key_roots || !multi_sig->hots_pks ||
        !multi_sig->rho_seeds || !multi_sig->proofs || !multi_sig->leaf_indices) {
        chipmunk_multi_signature_free(multi_sig);
        return -2;
    }

    multi_sig->signer_count = count;

    const chipmunk_hvc_poly_t *tree_root = chipmunk_tree_root(tree);
    if (!tree_root) {
        chipmunk_multi_signature_free(multi_sig);
        return -3;
    }
    memcpy(&multi_sig->tree_root, tree_root, sizeof(chipmunk_hvc_poly_t));
    // CR-D15.A: propagate the hasher seed so the verifier can rebuild the
    // same Ajtai hasher and run a full path check (no more hard-coded
    // {1..32} placeholder seed on the verify side).
    memcpy(multi_sig->hvc_hasher_seed, tree->hasher_seed, sizeof(multi_sig->hvc_hasher_seed));

    dap_hash_sha3_256_t message_hash;
    dap_hash_sha3_256(message, message_len, &message_hash);
    memcpy(multi_sig->message_hash, message_hash.raw, DAP_HASH_SHA3_256_SIZE);

    chipmunk_hots_signature_t *hots_sigs = calloc(count, sizeof(chipmunk_hots_signature_t));
    if (!hots_sigs) {
        chipmunk_multi_signature_free(multi_sig);
        return -2;
    }

    for (size_t i = 0; i < count; i++) {
        memcpy(&hots_sigs[i], &individual_sigs[i].hots_sig, sizeof(chipmunk_hots_signature_t));

        memcpy(&multi_sig->proofs[i], &individual_sigs[i].proof, sizeof(chipmunk_path_t));
        multi_sig->leaf_indices[i] = individual_sigs[i].leaf_index;

        // CR-D6/D7: full HOTS pk is required by the aggregate verify equation.
        memcpy(&multi_sig->hots_pks[i], &individual_sigs[i].hots_pk,
               sizeof(chipmunk_hots_public_key_t));
        memcpy(multi_sig->rho_seeds[i], individual_sigs[i].rho_seed, 32);

        chipmunk_public_key_t l_full_pk;
        s_hots_pk_to_full_pk(&individual_sigs[i].hots_pk, &l_full_pk);
        int l_rc_leaf = chipmunk_hots_pk_to_hvc_poly(&l_full_pk,
                                                     &multi_sig->public_key_roots[i]);
        if (l_rc_leaf != CHIPMUNK_ERROR_SUCCESS) {
            free(hots_sigs);
            chipmunk_multi_signature_free(multi_sig);
            return l_rc_leaf;
        }
    }

    chipmunk_randomizers_t randomizers;
    int ret = chipmunk_randomizers_from_pks(multi_sig->public_key_roots, count, &randomizers);
    if (ret != 0) {
        free(hots_sigs);
        chipmunk_multi_signature_free(multi_sig);
        return ret;
    }

    ret = chipmunk_hots_aggregate_with_randomizers(hots_sigs, randomizers.randomizers,
                                                   count, &multi_sig->aggregated_hots);

    free(hots_sigs);
    chipmunk_randomizers_free(&randomizers);

    if (ret != 0) {
        chipmunk_multi_signature_free(multi_sig);
        return ret;
    }

    return 0;
}

/**
 * @brief Verify an aggregated multi-signature.
 *
 * CR-D6/D7 rewrite (Round-4): this function enforces the full HOTS aggregate
 * identity in the NTT domain:
 *
 *   Σ_i A_i · σ_agg_i  ==  H(m) · Σ_j r_j · v0_j  +  Σ_j r_j · v1_j
 *
 * where `A` is the single shared HOTS public matrix supplied by
 * `chipmunk_hots_setup()` (the same one used by signers inside
 * `chipmunk_hots_keygen()` / `chipmunk_hots_sign()`), `σ_agg_i` is the
 * pre-aggregated signature polynomial produced by
 * `chipmunk_hots_aggregate_with_randomizers()`, and `r_j` is the per-signer
 * ternary randomizer derived deterministically from the concatenation of
 * all signers' HVC-leaf digests via SHA3-256 (see
 * `chipmunk_randomizers_from_pks`).
 *
 * Derivation: starting from the per-signer HOTS identity
 *
 *   Σ_i A_i · σ_ji  =  H(m) · v0_j + v1_j                              (HOTS)
 *
 * multiply both sides by the signer-specific randomizer polynomial r_j and
 * sum over j:
 *
 *   Σ_j Σ_i A_i · (r_j · σ_ji)
 *     =  Σ_j ( H(m) · r_j · v0_j + r_j · v1_j )
 *
 * The left-hand side can be rearranged as
 *   Σ_i A_i · Σ_j (r_j · σ_ji)  =  Σ_i A_i · σ_agg_i
 * because `chipmunk_hots_aggregate_with_randomizers` stores exactly that
 * accumulated sum.  The right-hand side factors cleanly into the
 * (H(m)·V0_sum + V1_sum) form used below.
 *
 * The previous implementation short-circuited this identity to a coefficient-
 * wise Hadamard check with ±10 % slack — equivalent to "ship anything, it
 * will verify" — and used a lossy HVC-hash stand-in for the v0 component.
 * Both layers of damage are replaced with strict equality on full
 * chipmunk_poly_t objects.
 *
 * NOTE on `rho_seed` (follow-up CR-D5): the current HOTS primitive ignores
 * the stored per-signer rho_seed and derives A from a fixed LCG seed inside
 * `chipmunk_hots_setup`.  The aggregate container still records rho_seeds
 * (they are ring-metadata) and we sanity-check cross-signer consistency to
 * preserve forward-compatibility with the upcoming CR-D5 remediation, but
 * the verification equation intentionally uses the same A the signers use.
 *
 * @return 1 if the aggregate identity holds AND every Merkle proof / leaf
 *           binding checks out,
 *         0 if verification fails (any branch),
 *         negative on hard error (NULL parameter, allocation failure, …).
 */
int chipmunk_verify_multi_signature(const chipmunk_multi_signature_t *multi_sig,
                                    const uint8_t *message,
                                    size_t message_len) {
    if (!multi_sig || !message || multi_sig->signer_count == 0) {
        return -1;
    }
    if (!multi_sig->hots_pks || !multi_sig->public_key_roots || !multi_sig->rho_seeds) {
        // CR-D6/D7: legacy blobs without hots_pks / rho_seeds cannot be
        // verified under the corrected identity.  Refuse rather than silently
        // accepting.
        log_it(L_ERROR, "Multi-signature missing hots_pks / rho_seeds; cannot verify");
        return 0;
    }

    // 1. Message-hash binding.
    dap_hash_sha3_256_t l_computed_hash;
    dap_hash_sha3_256(message, message_len, &l_computed_hash);
    if (memcmp(l_computed_hash.raw, multi_sig->message_hash, DAP_HASH_SHA3_256_SIZE) != 0) {
        log_it(L_ERROR, "Multi-sig message-hash binding mismatch");
        return 0;
    }

    // 2. Tree-root verification (when a tree is attached).
    bool l_has_tree_root = false;
    for (int i = 0; i < CHIPMUNK_N && !l_has_tree_root; i++) {
        if (multi_sig->tree_root.coeffs[i] != 0) {
            l_has_tree_root = true;
        }
    }
    if (l_has_tree_root) {
        // CR-D15.A: use the hasher seed that the aggregator recorded when
        // the tree was built.  Any discrepancy between signer-side and
        // verifier-side matrices would now surface as a path failure
        // instead of silently accepting any proof (as the old hardcoded
        // {1..32} seed did).
        bool l_seed_nonzero = false;
        for (int i = 0; i < 32 && !l_seed_nonzero; ++i) {
            if (multi_sig->hvc_hasher_seed[i] != 0) l_seed_nonzero = true;
        }
        if (!l_seed_nonzero) {
            log_it(L_ERROR, "Multi-signature carries a zero hvc_hasher_seed; refusing to verify Merkle paths");
            return 0;
        }
        chipmunk_hvc_hasher_t l_hasher;
        if (chipmunk_hvc_hasher_init(&l_hasher, multi_sig->hvc_hasher_seed) != 0) {
            log_it(L_ERROR, "Failed to initialise HVC hasher from multi-sig seed");
            return -1;
        }
        for (size_t j = 0; j < multi_sig->signer_count; j++) {
            if (!chipmunk_path_verify(&multi_sig->proofs[j],
                                       &multi_sig->public_key_roots[j],
                                       &multi_sig->tree_root,
                                       &l_hasher)) {
                log_it(L_ERROR, "Tree-root verification failed for signer %zu", j);
                return 0;
            }
        }
    }

    // 3. Per-signer HVC-leaf binding: the claimed hots_pks[j] must hash to
    //    public_key_roots[j] (which is also the Merkle leaf when a tree is
    //    attached).  Without this check an adversary could swap hots_pks[j]
    //    for a self-chosen pair (v0', v1') and rebuild an aggregate that
    //    satisfies the identity — CR-D6.
    for (size_t j = 0; j < multi_sig->signer_count; j++) {
        if (!s_verify_pk_leaf_binding(&multi_sig->hots_pks[j],
                                      &multi_sig->public_key_roots[j])) {
            log_it(L_ERROR, "HVC-leaf binding mismatch for signer %zu", j);
            return 0;
        }
    }

    // 4. Load the shared HOTS public matrix A.  This MUST be the same A the
    //    signers used when producing σ_ji (see chipmunk_hots_sign), otherwise
    //    the aggregate identity cannot hold.  The matrix is already in NTT
    //    domain after chipmunk_hots_setup (see chipmunk_hots_verify which
    //    passes &a_params->a[i] directly to chipmunk_poly_mul_ntt without a
    //    second forward transform).
    chipmunk_hots_params_t l_params;
    if (chipmunk_hots_setup(&l_params) != 0) {
        log_it(L_ERROR, "Failed to load shared HOTS public matrix A");
        return -1;
    }

    // 5. Derive randomizers deterministically from the ring of leaf digests.
    chipmunk_randomizers_t l_randomizers;
    int l_rc = chipmunk_randomizers_from_pks(multi_sig->public_key_roots,
                                             multi_sig->signer_count, &l_randomizers);
    if (l_rc != 0) {
        return l_rc;
    }

    // 6. LHS = Σ_i A_i · σ_agg_i  (NTT domain).  σ_agg is stored in the time
    //    domain to match the single-HOTS wire format, so we re-canonicalise
    //    and forward-NTT before the pointwise product.
    chipmunk_poly_t l_lhs_ntt;
    memset(&l_lhs_ntt, 0, sizeof(l_lhs_ntt));
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        chipmunk_poly_t l_sigma_ntt = multi_sig->aggregated_hots.sigma[i];
        for (int k = 0; k < CHIPMUNK_N; k++) {
            l_sigma_ntt.coeffs[k] = s_canonicalize_mod_q(l_sigma_ntt.coeffs[k]);
        }
        chipmunk_ntt(l_sigma_ntt.coeffs);

        chipmunk_poly_t l_term;
        chipmunk_poly_mul_ntt(&l_term, &l_params.a[i], &l_sigma_ntt);
        chipmunk_poly_add_ntt(&l_lhs_ntt, &l_lhs_ntt, &l_term);
    }

    // 7. RHS components in NTT domain:
    //      V0_sum = Σ_j NTT(r_j) ⊙ NTT(v0_j)
    //      V1_sum = Σ_j NTT(r_j) ⊙ NTT(v1_j)
    chipmunk_poly_t l_v0_sum_ntt, l_v1_sum_ntt;
    memset(&l_v0_sum_ntt, 0, sizeof(l_v0_sum_ntt));
    memset(&l_v1_sum_ntt, 0, sizeof(l_v1_sum_ntt));

    for (size_t j = 0; j < multi_sig->signer_count; j++) {
        chipmunk_poly_t l_r_ntt;
        s_randomizer_to_poly_ntt(&l_randomizers.randomizers[j], &l_r_ntt);

        chipmunk_poly_t l_v0 = multi_sig->hots_pks[j].v0;
        chipmunk_poly_t l_v1 = multi_sig->hots_pks[j].v1;
        for (int k = 0; k < CHIPMUNK_N; k++) {
            l_v0.coeffs[k] = s_canonicalize_mod_q(l_v0.coeffs[k]);
            l_v1.coeffs[k] = s_canonicalize_mod_q(l_v1.coeffs[k]);
        }
        chipmunk_ntt(l_v0.coeffs);
        chipmunk_ntt(l_v1.coeffs);

        chipmunk_poly_t l_prod0, l_prod1;
        chipmunk_poly_mul_ntt(&l_prod0, &l_r_ntt, &l_v0);
        chipmunk_poly_mul_ntt(&l_prod1, &l_r_ntt, &l_v1);

        chipmunk_poly_add_ntt(&l_v0_sum_ntt, &l_v0_sum_ntt, &l_prod0);
        chipmunk_poly_add_ntt(&l_v1_sum_ntt, &l_v1_sum_ntt, &l_prod1);
    }

    chipmunk_randomizers_free(&l_randomizers);

    // 8. H(m) in NTT domain (same derivation as chipmunk_hots_verify).
    chipmunk_poly_t l_hm;
    if (chipmunk_poly_from_hash(&l_hm, message, message_len) != 0) {
        log_it(L_ERROR, "Failed to derive H(m) polynomial");
        return -1;
    }
    chipmunk_ntt(l_hm.coeffs);

    // 9. RHS = H(m) · V0_sum + V1_sum  (NTT domain).
    chipmunk_poly_t l_rhs_ntt, l_hm_v0_ntt;
    chipmunk_poly_mul_ntt(&l_hm_v0_ntt, &l_hm, &l_v0_sum_ntt);
    chipmunk_poly_add_ntt(&l_rhs_ntt, &l_hm_v0_ntt, &l_v1_sum_ntt);

    // 10. Strict equality check (no slack, no tolerance).  chipmunk_poly_equal
    //      canonicalises both operands, so this is a true Rq-equality.
    if (!chipmunk_poly_equal(&l_lhs_ntt, &l_rhs_ntt)) {
        log_it(L_ERROR, "Aggregate HOTS identity does not hold");
        return 0;
    }

    debug_if(s_debug_more, L_INFO,
             "Multi-signature verified: aggregate identity holds for %zu signer(s)",
             multi_sig->signer_count);
    return 1;
}

// === Memory Management ===

/**
 * Free multi-signature structure
 */
void chipmunk_multi_signature_free(chipmunk_multi_signature_t *multi_sig) {
    if (multi_sig) {
        if (multi_sig->public_key_roots) {
            free(multi_sig->public_key_roots);
            multi_sig->public_key_roots = NULL;
        }
        if (multi_sig->hots_pks) {
            free(multi_sig->hots_pks);
            multi_sig->hots_pks = NULL;
        }
        if (multi_sig->rho_seeds) {
            free(multi_sig->rho_seeds);
            multi_sig->rho_seeds = NULL;
        }
        if (multi_sig->proofs) {
            free(multi_sig->proofs);
            multi_sig->proofs = NULL;
        }
        if (multi_sig->leaf_indices) {
            free(multi_sig->leaf_indices);
            multi_sig->leaf_indices = NULL;
        }
        multi_sig->signer_count = 0;
    }
}

/**
 * Free individual signature structure
 */
void chipmunk_individual_signature_free(chipmunk_individual_sig_t *individual_sig) {
    if (individual_sig) {
        // Individual signature doesn't allocate dynamic memory,
        // just clear the structure
        memset(individual_sig, 0, sizeof(chipmunk_individual_sig_t));
    }
}

// === Batch Verification ===
//
// CR-D6/D7 Round-4: the batch verifier is strict — it dispatches every
// stored aggregate to chipmunk_verify_multi_signature(), which enforces
// the full NTT aggregate identity.  No heuristics, no slack.  See
// chipmunk_batch_verify() below for the rationale behind the current
// per-signature dispatch and the follow-up needed to enable true
// amortised batch aggregation once CR-D5 (A-from-rho_seed) lands.

/**
 * Initialize batch verification context
 */
int chipmunk_batch_context_init(chipmunk_batch_context_t *context,
                                size_t max_signatures) {
    if (!context || max_signatures == 0) {
        return -1;
    }

    memset(context, 0, sizeof(*context));
    context->signatures = calloc(max_signatures, sizeof(chipmunk_multi_signature_t));
    context->messages = calloc(max_signatures, sizeof(const uint8_t*));
    context->message_lengths = calloc(max_signatures, sizeof(size_t));

    if (!context->signatures || !context->messages || !context->message_lengths) {
        chipmunk_batch_context_free(context);
        return -2;
    }

    context->signature_count = 0;
    context->capacity = max_signatures;
    return 0;
}

/**
 * Add signature to batch verification context
 */
int chipmunk_batch_add_signature(chipmunk_batch_context_t *context,
                                 const chipmunk_multi_signature_t *multi_sig,
                                 const uint8_t *message,
                                 size_t message_len) {
    if (!context || !multi_sig || !message || !context->signatures ||
        !context->messages || !context->message_lengths) {
        return -1;
    }
    if (message_len == 0) {
        return -3;
    }
    if (context->signature_count >= context->capacity) {
        return -2;
    }

    // Shallow copy — multi-signature arrays are still owned by the caller.
    memcpy(&context->signatures[context->signature_count], multi_sig,
           sizeof(chipmunk_multi_signature_t));

    // Caller must keep the message buffer alive for the lifetime of the batch
    // context; we never copy it to avoid unnecessary allocations.
    context->messages[context->signature_count] = message;
    context->message_lengths[context->signature_count] = message_len;

    context->signature_count++;
    return 0;
}

/**
 * @brief Batch-verify every multi-signature in the context.
 *
 * CR-D6/D7 rewrite (Round-4): the previous implementation built an ad-hoc
 * "aggregated left/right side" using the lossy HVC-projected public_key_root
 * as a stand-in for v0, a random linear combination that never cancelled,
 * and a 20 %-coefficient-mismatch threshold — which together guaranteed
 * that roughly any non-zero blob passed.  Batch verification MUST be at
 * least as strict as verifying each aggregate individually, so we now
 * simply dispatch to chipmunk_verify_multi_signature() (which enforces the
 * full NTT aggregate identity).
 *
 * Performance note: the per-multi-signature cost is already dominated by
 * the GAMMA forward-NTTs of σ_agg and the V0/V1 accumulations.  A proper
 * amortised batch verifier would randomise the scalar multiplicands across
 * contexts, but there is no safe way to do so while the underlying HOTS
 * primitive still uses a global hard-coded A (CR-D5 follow-up).  Once the
 * A-from-rho_seed remediation lands, we can revisit true batch aggregation;
 * for now "correct but per-signature" is the only defensible behaviour.
 *
 * @return 1 if every aggregate verifies, 0 on any failure, negative on
 *         hard error (NULL/context, empty batch, …).
 */
int chipmunk_batch_verify(const chipmunk_batch_context_t *context) {
    if (!context || context->signature_count == 0) {
        return -1;
    }
    if (!context->signatures || !context->messages || !context->message_lengths) {
        log_it(L_ERROR, "Batch context is missing one of signatures/messages/lengths");
        return -1;
    }

    log_it(L_DEBUG, "Starting strict batch verification for %zu multi-signatures",
           context->signature_count);

    for (size_t i = 0; i < context->signature_count; i++) {
        const uint8_t *l_msg = context->messages[i];
        size_t l_msg_len = context->message_lengths[i];
        if (!l_msg || l_msg_len == 0) {
            log_it(L_ERROR, "Batch entry %zu has empty message", i);
            return -2;
        }

        int l_rc = chipmunk_verify_multi_signature(&context->signatures[i],
                                                   l_msg, l_msg_len);
        if (l_rc < 0) {
            log_it(L_ERROR, "Batch entry %zu hard-errored: %d", i, l_rc);
            return l_rc;
        }
        if (l_rc != 1) {
            log_it(L_ERROR, "Batch entry %zu failed aggregate identity", i);
            return 0;
        }
    }

    log_it(L_DEBUG, "Strict batch verification succeeded for %zu multi-signatures",
           context->signature_count);
    return 1;
}

/**
 * Free batch verification context
 */
void chipmunk_batch_context_free(chipmunk_batch_context_t *context) {
    if (context) {
        if (context->signatures) {
            free(context->signatures);
            context->signatures = NULL;
        }
        if (context->messages) {
            free(context->messages);
            context->messages = NULL;
        }
        if (context->message_lengths) {
            free(context->message_lengths);
            context->message_lengths = NULL;
        }
        context->capacity = 0;
        context->signature_count = 0;
    }
} 