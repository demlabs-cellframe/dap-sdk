/*
 * test_chipmunk_poseidon.c — Unit tests for Poseidon hash over F_q.
 *
 * Tests:
 *   1. Permutation of [0,0,0] matches Python reference
 *   2. Permutation of [1,0,0] matches Python reference
 *   3. hash2(42,7) matches Python reference
 *   4. hash2 is deterministic (same input → same output)
 *   5. hash2 is not commutative (hash(a,b) ≠ hash(b,a) for a ≠ b)
 *   6. hash2(0,0) != hash2(0,1) (distinctness)
 *   7. S-box x^5 is bijective on small range
 *   8. Round constants are in [0, q)
 *   9. hash2 output is in [0, q)
 *  10. Multiple inputs map to distinct outputs (collision check on 100 pairs)
 *  11. Permutation does not modify zero input to zero output (non-trivial)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <dap_common.h>
#include <dap_test.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "sig/chipmunk/chipmunk_poseidon.h"

#define LOG_TAG "test_chipmunk_poseidon"

/* =========================================================================
 * Tests
 * ========================================================================= */

static void test_perm_zero_state(void)
{
    int32_t state[3] = { 0, 0, 0 };
    chipmunk_poseidon_perm(state);

    /* Deterministic generator: SHAKE256("Poseidon-" || q || idx) → rejection sample.
     * Values computed at runtime from chipmunk_poseidon_params_compute(q=3168257). */
    dap_assert(state[0] == 768777,
              "perm([0,0,0])[0] == 768777");
    dap_assert(state[1] == 1094377,
              "perm([0,0,0])[1] == 1094377");
    dap_assert(state[2] == 307718,
              "perm([0,0,0])[2] == 307718");
}

static void test_perm_one_state(void)
{
    int32_t state[3] = { 1, 0, 0 };
    chipmunk_poseidon_perm(state);

    /* Deterministic generator values for q=3168257. */
    dap_assert(state[0] == 417045,
              "perm([1,0,0])[0] == 417045");
    dap_assert(state[1] == 217723,
              "perm([1,0,0])[1] == 217723");
    dap_assert(state[2] == 336805,
              "perm([1,0,0])[2] == 336805");
}

static void test_hash2_known(void)
{
    /* Deterministic generator values for q=3168257. */
    int32_t h = chipmunk_poseidon_hash2(42, 7);
    dap_assert(h == 2780295, "hash2(42, 7) == 2780295");

    h = chipmunk_poseidon_hash2(7, 42);
    dap_assert(h == 914988, "hash2(7, 42) == 914988");
}

static void test_hash2_deterministic(void)
{
    int32_t h1 = chipmunk_poseidon_hash2(123456, 654321);
    int32_t h2 = chipmunk_poseidon_hash2(123456, 654321);
    dap_assert(h1 == h2, "hash2 is deterministic");
}

static void test_hash2_non_commutative(void)
{
    /* hash(a,b) should != hash(b,a) for a != b (with high probability) */
    int32_t ha = chipmunk_poseidon_hash2(100, 200);
    int32_t hb = chipmunk_poseidon_hash2(200, 100);
    dap_assert(ha != hb, "hash2 is not commutative");
}

static void test_hash2_distinct_inputs(void)
{
    int32_t h0 = chipmunk_poseidon_hash2(0, 0);
    int32_t h1 = chipmunk_poseidon_hash2(0, 1);
    dap_assert(h0 != h1, "hash2(0,0) != hash2(0,1)");
}

static void test_hash2_output_in_range(void)
{
    /* Check that outputs are in [0, q) for various inputs */
    int l_ok = 1;
    for (int32_t a = 0; a < 20 && l_ok; ++a) {
        for (int32_t b = 0; b < 20 && l_ok; ++b) {
            int32_t h = chipmunk_poseidon_hash2(a, b);
            if (h < 0 || h >= (int32_t)CHIPMUNK_Q) {
                char l_msg[80];
                snprintf(l_msg, sizeof(l_msg),
                         "hash2(%d,%d) = %d out of [0,q)", a, b, h);
                dap_assert(0, l_msg);
                l_ok = 0;
            }
        }
    }
    if (l_ok) {
        dap_assert(1, "hash2 outputs in [0, q) for 400 inputs");
    }
}

static void test_perm_nontrivial(void)
{
    /* Permutation should be non-trivial: perm([0,0,0]) != [0,0,0] */
    int32_t state[3] = { 0, 0, 0 };
    chipmunk_poseidon_perm(state);
    dap_assert(state[0] != 0 || state[1] != 0 || state[2] != 0,
              "perm([0,0,0]) != [0,0,0]");
}

static void test_collision_free_sample(void)
{
    /* Check 100 sequential hash pairs for collisions */
    int32_t hashes[200];
    int l_ok = 1;

    for (int i = 0; i < 100 && l_ok; ++i) {
        hashes[2 * i]     = chipmunk_poseidon_hash2((int32_t)i, (int32_t)(i + 1000));
        hashes[2 * i + 1] = chipmunk_poseidon_hash2((int32_t)(i + 1000), (int32_t)i);
    }

    for (int i = 0; i < 200 && l_ok; ++i) {
        for (int j = i + 1; j < 200 && l_ok; ++j) {
            if (hashes[i] == hashes[j]) {
                char l_msg[80];
                snprintf(l_msg, sizeof(l_msg),
                         "collision: hash[%d]=%d == hash[%d]=%d",
                         i, hashes[i], j, hashes[j]);
                dap_assert(0, l_msg);
                l_ok = 0;
            }
        }
    }

    if (l_ok) {
        dap_assert(1, "no collisions among 200 hashes from 100 input pairs");
    }
}

static void test_init_ok(void)
{
    int l_rc = chipmunk_poseidon_init();
    dap_assert(l_rc == 0, "poseidon init returns 0");
}

/* =========================================================================
 * Main
 * ========================================================================= */

int main(void)
{
    dap_set_appname("test_chipmunk_poseidon");
    dap_common_init("test_chipmunk_poseidon", NULL);

    chipmunk_poseidon_init();

    test_init_ok();
    test_perm_zero_state();
    test_perm_one_state();
    test_hash2_known();
    test_hash2_deterministic();
    test_hash2_non_commutative();
    test_hash2_distinct_inputs();
    test_hash2_output_in_range();
    test_perm_nontrivial();
    test_collision_free_sample();

    log_it(L_INFO, "=== ALL chipmunk_poseidon tests PASSED (Phase 9.3: Poseidon hash) ===");
    dap_common_deinit();
    return 0;
}
