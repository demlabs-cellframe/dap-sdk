/*
 * CR-D15.A regression test — ensures the Chipmunk HVC Merkle layer keeps the
 * honesty guarantees that landed in Round-5:
 *
 *   1. Hasher is deterministic for the same seed (same NTT matrices materialise).
 *   2. Hasher is sensitive to seed changes (completely different matrices).
 *   3. HOTS-pk-to-HVC-leaf digest is collision-resistant against trivial
 *      coefficient tweaks in v0 or v1.
 *   4. Path verification binds the claimed leaf to the path's parity slot —
 *      swapping the leaf for any unrelated polynomial must fail.
 *   5. Tampering with a single inner node of a valid path must fail.
 *   6. Wrong-index replay (submitting a valid path for leaf j while claiming
 *      leaf k) must fail.
 *   7. Swapping the hasher seed on the verifier side must fail the path.
 *
 * These are the exact exploits the pre-CR-D15.A implementation silently
 * accepted because (a) chipmunk_hvc_hash_decom_then_hash was linear
 * (x + y mod q), (b) chipmunk_path_verify only checked the level-0 hash, and
 * (c) chipmunk_hvc_hasher_init used a predictable LCG driven by one seed byte.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "dap_common.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_tree.h"

#define LOG_TAG "test_hvc_honest"

static int s_fail(const char *a_what)
{
    log_it(L_ERROR, "❌ %s", a_what);
    return 1;
}

static int s_pass(const char *a_what)
{
    log_it(L_INFO, "✅ %s", a_what);
    return 0;
}

// Cheap pseudo-random leaf: fill coeffs with a deterministic function of the
// index so that every leaf is distinct.  This deliberately does NOT use
// chipmunk_hots_pk_to_hvc_poly so that the tree construction and verification
// paths are exercised independently from the leaf-digest primitive.
static void s_fill_leaf(chipmunk_hvc_poly_t *a_leaf, uint32_t a_index)
{
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        int32_t l_v = (int32_t)((a_index + 1) * 7919u + (uint32_t)i * 31u);
        a_leaf->coeffs[i] = l_v % CHIPMUNK_HVC_Q;
    }
}

static int s_test_hasher_determinism(void)
{
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = (uint8_t)(0x5a ^ i);

    chipmunk_hvc_hasher_t l_h1, l_h2;
    if (chipmunk_hvc_hasher_init(&l_h1, l_seed) != CHIPMUNK_ERROR_SUCCESS) {
        return s_fail("hasher_init[1] failed");
    }
    if (chipmunk_hvc_hasher_init(&l_h2, l_seed) != CHIPMUNK_ERROR_SUCCESS) {
        return s_fail("hasher_init[2] failed");
    }
    // The NTT-form matrices must be bit-identical across two init calls —
    // the only non-determinism that could sneak in would be uninit-stack
    // residue in s_hvc_sample_poly_uniform (similar to the CR-D5 chipmunk_hots
    // bug).  Fail loudly if the invariant breaks.
    if (memcmp(l_h1.matrix_a_left_ntt,  l_h2.matrix_a_left_ntt,  sizeof(l_h1.matrix_a_left_ntt))  != 0 ||
        memcmp(l_h1.matrix_a_right_ntt, l_h2.matrix_a_right_ntt, sizeof(l_h1.matrix_a_right_ntt)) != 0) {
        return s_fail("hasher is non-deterministic for the same seed");
    }
    return s_pass("hasher determinism for same seed");
}

static int s_test_hasher_seed_sensitivity(void)
{
    uint8_t l_seed1[32]; memset(l_seed1, 0x01, 32);
    uint8_t l_seed2[32]; memset(l_seed2, 0x02, 32);

    chipmunk_hvc_hasher_t l_h1, l_h2;
    if (chipmunk_hvc_hasher_init(&l_h1, l_seed1) != CHIPMUNK_ERROR_SUCCESS ||
        chipmunk_hvc_hasher_init(&l_h2, l_seed2) != CHIPMUNK_ERROR_SUCCESS) {
        return s_fail("hasher_init failed");
    }
    size_t l_diff = 0;
    const int32_t *l_a = (const int32_t *)l_h1.matrix_a_left_ntt;
    const int32_t *l_b = (const int32_t *)l_h2.matrix_a_left_ntt;
    size_t l_total = sizeof(l_h1.matrix_a_left_ntt) / sizeof(int32_t);
    for (size_t i = 0; i < l_total; ++i) {
        if (l_a[i] != l_b[i]) ++l_diff;
    }
    // Expect an overwhelming majority of coefficients to differ; anything
    // below 50 % would indicate seed-insensitive sampling (the pre-D15.A LCG
    // ignored all but the first seed byte and derived a near-identical
    // matrix for any pair of seeds with the same leading byte).
    double l_ratio = (double)l_diff / (double)l_total;
    if (l_ratio < 0.90) {
        log_it(L_ERROR, "seed sensitivity too low: %.3f (expected ≥ 0.90)", l_ratio);
        return 1;
    }
    return s_pass("hasher seed sensitivity");
}

static int s_test_leaf_digest_collision_resistance(void)
{
    chipmunk_public_key_t l_pk1;
    memset(&l_pk1, 0, sizeof(l_pk1));
    for (int i = 0; i < 32; ++i)     l_pk1.rho_seed[i] = (uint8_t)(i * 3u);
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        l_pk1.v0.coeffs[i] = (i * 131) % CHIPMUNK_Q;
        l_pk1.v1.coeffs[i] = (i * 197 + 42) % CHIPMUNK_Q;
    }

    chipmunk_hvc_poly_t l_digest1, l_digest2;
    if (chipmunk_hots_pk_to_hvc_poly(&l_pk1, &l_digest1) != CHIPMUNK_ERROR_SUCCESS) {
        return s_fail("pk_to_hvc_poly[1] failed");
    }

    // Perturb a single coefficient of v0; the digest must change.
    chipmunk_public_key_t l_pk2 = l_pk1;
    l_pk2.v0.coeffs[0] = (l_pk1.v0.coeffs[0] + 1) % CHIPMUNK_Q;
    if (chipmunk_hots_pk_to_hvc_poly(&l_pk2, &l_digest2) != CHIPMUNK_ERROR_SUCCESS) {
        return s_fail("pk_to_hvc_poly[2] failed");
    }
    if (memcmp(&l_digest1, &l_digest2, sizeof(l_digest1)) == 0) {
        return s_fail("leaf digest did not change after single v0 coeff tweak");
    }

    // Perturb rho_seed; digest must change even though v0, v1 unchanged
    // (because rho_seed is part of the canonical HOTS pk identity).
    chipmunk_public_key_t l_pk3 = l_pk1;
    l_pk3.rho_seed[0] ^= 0xa5;
    chipmunk_hvc_poly_t l_digest3;
    if (chipmunk_hots_pk_to_hvc_poly(&l_pk3, &l_digest3) != CHIPMUNK_ERROR_SUCCESS) {
        return s_fail("pk_to_hvc_poly[3] failed");
    }
    if (memcmp(&l_digest1, &l_digest3, sizeof(l_digest1)) == 0) {
        return s_fail("leaf digest did not change after rho_seed tweak");
    }
    return s_pass("leaf digest collision resistance (v0, rho_seed)");
}

static int s_build_tree(chipmunk_tree_t *a_tree, chipmunk_hvc_hasher_t *a_hasher,
                        chipmunk_hvc_poly_t *a_leaves, size_t a_count)
{
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = (uint8_t)(0xaa ^ (i * 13));
    if (chipmunk_hvc_hasher_init(a_hasher, l_seed) != CHIPMUNK_ERROR_SUCCESS) {
        return -1;
    }
    for (size_t i = 0; i < a_count; ++i) {
        s_fill_leaf(&a_leaves[i], (uint32_t)i);
    }
    return chipmunk_tree_new_with_leaf_nodes(a_tree, a_leaves, a_count, a_hasher);
}

static int s_test_leaf_binding(void)
{
    const size_t k_leaves = 16;
    chipmunk_tree_t l_tree;
    chipmunk_hvc_hasher_t l_hasher;
    chipmunk_hvc_poly_t l_leaves[16];

    if (s_build_tree(&l_tree, &l_hasher, l_leaves, k_leaves) != CHIPMUNK_ERROR_SUCCESS) {
        return s_fail("tree build failed");
    }

    const chipmunk_hvc_poly_t *l_root = chipmunk_tree_root(&l_tree);

    chipmunk_path_t l_path;
    if (chipmunk_tree_gen_proof(&l_tree, 3, &l_path) != CHIPMUNK_ERROR_SUCCESS) {
        chipmunk_tree_free(&l_tree);
        return s_fail("gen_proof failed");
    }

    // Honest verify should succeed.
    if (!chipmunk_path_verify(&l_path, &l_leaves[3], l_root, &l_hasher)) {
        chipmunk_path_free(&l_path);
        chipmunk_tree_free(&l_tree);
        return s_fail("honest verify rejected");
    }

    // Submitting someone else's leaf on the same path must fail.
    if (chipmunk_path_verify(&l_path, &l_leaves[5], l_root, &l_hasher)) {
        chipmunk_path_free(&l_path);
        chipmunk_tree_free(&l_tree);
        return s_fail("leaf-swap attack accepted");
    }

    // Submitting a wholly synthetic leaf must fail.
    chipmunk_hvc_poly_t l_fake;
    s_fill_leaf(&l_fake, 0xdeadbeefu);
    if (chipmunk_path_verify(&l_path, &l_fake, l_root, &l_hasher)) {
        chipmunk_path_free(&l_path);
        chipmunk_tree_free(&l_tree);
        return s_fail("synthetic-leaf attack accepted");
    }

    chipmunk_path_free(&l_path);
    chipmunk_tree_free(&l_tree);
    return s_pass("leaf binding (honest / leaf-swap / synthetic)");
}

static int s_test_path_tamper(void)
{
    const size_t k_leaves = 32;
    chipmunk_tree_t l_tree;
    chipmunk_hvc_hasher_t l_hasher;
    chipmunk_hvc_poly_t l_leaves[32];

    if (s_build_tree(&l_tree, &l_hasher, l_leaves, k_leaves) != CHIPMUNK_ERROR_SUCCESS) {
        return s_fail("tree build failed");
    }
    const chipmunk_hvc_poly_t *l_root = chipmunk_tree_root(&l_tree);

    chipmunk_path_t l_path;
    if (chipmunk_tree_gen_proof(&l_tree, 7, &l_path) != CHIPMUNK_ERROR_SUCCESS) {
        chipmunk_tree_free(&l_tree);
        return s_fail("gen_proof failed");
    }
    if (!chipmunk_path_verify(&l_path, &l_leaves[7], l_root, &l_hasher)) {
        chipmunk_path_free(&l_path);
        chipmunk_tree_free(&l_tree);
        return s_fail("honest verify rejected before tamper");
    }

    // Tamper with an inner node (level 1) by flipping a single coefficient.
    l_path.nodes[1].left.coeffs[0] ^= 1;
    if (chipmunk_path_verify(&l_path, &l_leaves[7], l_root, &l_hasher)) {
        chipmunk_path_free(&l_path);
        chipmunk_tree_free(&l_tree);
        return s_fail("inner-node tamper (level 1) accepted");
    }
    l_path.nodes[1].left.coeffs[0] ^= 1;  // restore

    // Tamper with the sibling at level 0 (different parity slot).
    l_path.nodes[0].right.coeffs[5] = (l_path.nodes[0].right.coeffs[5] + 1) % CHIPMUNK_HVC_Q;
    if (chipmunk_path_verify(&l_path, &l_leaves[7], l_root, &l_hasher)) {
        chipmunk_path_free(&l_path);
        chipmunk_tree_free(&l_tree);
        return s_fail("level-0 sibling tamper accepted");
    }

    chipmunk_path_free(&l_path);
    chipmunk_tree_free(&l_tree);
    return s_pass("path tamper detection (levels 0 and 1)");
}

static int s_test_wrong_index_replay(void)
{
    const size_t k_leaves = 32;
    chipmunk_tree_t l_tree;
    chipmunk_hvc_hasher_t l_hasher;
    chipmunk_hvc_poly_t l_leaves[32];

    if (s_build_tree(&l_tree, &l_hasher, l_leaves, k_leaves) != CHIPMUNK_ERROR_SUCCESS) {
        return s_fail("tree build failed");
    }
    const chipmunk_hvc_poly_t *l_root = chipmunk_tree_root(&l_tree);

    chipmunk_path_t l_path_2, l_path_9;
    if (chipmunk_tree_gen_proof(&l_tree, 2, &l_path_2) != CHIPMUNK_ERROR_SUCCESS ||
        chipmunk_tree_gen_proof(&l_tree, 9, &l_path_9) != CHIPMUNK_ERROR_SUCCESS) {
        chipmunk_tree_free(&l_tree);
        return s_fail("gen_proof failed");
    }

    // Replay leaf[2] against path for leaf 9 — should fail (leaf binding or
    // inner hash mismatch).
    if (chipmunk_path_verify(&l_path_9, &l_leaves[2], l_root, &l_hasher)) {
        chipmunk_path_free(&l_path_2);
        chipmunk_path_free(&l_path_9);
        chipmunk_tree_free(&l_tree);
        return s_fail("replay leaf[2] on path[9] accepted");
    }

    // Honest verifications must still succeed.
    if (!chipmunk_path_verify(&l_path_2, &l_leaves[2], l_root, &l_hasher) ||
        !chipmunk_path_verify(&l_path_9, &l_leaves[9], l_root, &l_hasher)) {
        chipmunk_path_free(&l_path_2);
        chipmunk_path_free(&l_path_9);
        chipmunk_tree_free(&l_tree);
        return s_fail("honest verify rejected after replay check");
    }

    chipmunk_path_free(&l_path_2);
    chipmunk_path_free(&l_path_9);
    chipmunk_tree_free(&l_tree);
    return s_pass("wrong-index replay detection");
}

static int s_test_hasher_seed_swap(void)
{
    const size_t k_leaves = 16;
    chipmunk_hvc_poly_t l_leaves[16];
    chipmunk_tree_t l_tree;
    chipmunk_hvc_hasher_t l_hasher_signer, l_hasher_attacker;

    if (s_build_tree(&l_tree, &l_hasher_signer, l_leaves, k_leaves) != CHIPMUNK_ERROR_SUCCESS) {
        return s_fail("tree build failed");
    }
    const chipmunk_hvc_poly_t *l_root = chipmunk_tree_root(&l_tree);

    chipmunk_path_t l_path;
    if (chipmunk_tree_gen_proof(&l_tree, 11, &l_path) != CHIPMUNK_ERROR_SUCCESS) {
        chipmunk_tree_free(&l_tree);
        return s_fail("gen_proof failed");
    }

    // Verify with a hasher initialised from a different seed — this is what
    // a hostile aggregator would send to a verifier to make path_verify
    // accept arbitrary inclusion claims.  Must fail.
    uint8_t l_bad_seed[32];
    for (int i = 0; i < 32; ++i) l_bad_seed[i] = (uint8_t)(0x33 ^ i);
    if (chipmunk_hvc_hasher_init(&l_hasher_attacker, l_bad_seed) != CHIPMUNK_ERROR_SUCCESS) {
        chipmunk_path_free(&l_path);
        chipmunk_tree_free(&l_tree);
        return s_fail("attacker hasher_init failed");
    }
    if (chipmunk_path_verify(&l_path, &l_leaves[11], l_root, &l_hasher_attacker)) {
        chipmunk_path_free(&l_path);
        chipmunk_tree_free(&l_tree);
        return s_fail("hasher-seed swap accepted");
    }

    // Sanity: honest hasher still accepts.
    if (!chipmunk_path_verify(&l_path, &l_leaves[11], l_root, &l_hasher_signer)) {
        chipmunk_path_free(&l_path);
        chipmunk_tree_free(&l_tree);
        return s_fail("honest verify rejected after seed swap test");
    }

    chipmunk_path_free(&l_path);
    chipmunk_tree_free(&l_tree);
    return s_pass("hasher seed swap detection");
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    dap_log_level_set(L_INFO);
    log_it(L_NOTICE, "=== CR-D15.A HVC honesty regression ===");

    int l_failed = 0;
    l_failed += s_test_hasher_determinism();
    l_failed += s_test_hasher_seed_sensitivity();
    l_failed += s_test_leaf_digest_collision_resistance();
    l_failed += s_test_leaf_binding();
    l_failed += s_test_path_tamper();
    l_failed += s_test_wrong_index_replay();
    l_failed += s_test_hasher_seed_swap();

    if (l_failed == 0) {
        log_it(L_NOTICE, "✅ All HVC-honest regression tests passed");
        return 0;
    }
    log_it(L_ERROR, "❌ %d HVC-honest regression tests failed", l_failed);
    return 1;
}
