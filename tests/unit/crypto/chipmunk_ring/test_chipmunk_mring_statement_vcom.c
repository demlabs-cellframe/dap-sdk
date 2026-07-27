/*
 * test_chipmunk_mring_statement_vcom.c — MRNG M3.1 vector-commitment.
 *
 * CR-11.G Phase 7.7 / task_ac273cea.  Validates the building blocks of
 * the unified statement (G2 v2 §A1, REL-6):
 *
 *   T1. derive_vcom_generators is deterministic in ring_hash.
 *   T2. derive_vcom_generators yields non-trivial polynomials for the
 *       projection `a` and every randomness lane H'_j.
 *   T3. Distinct ring_hash values give distinct generators (domain sep).
 *   T4. vcom_pack_b rejects non-bit values and tolerates 0/1.
 *   T5. vcom_commit is HOMOMORPHIC in (b, r_b): for any (b, r_b) and
 *       (b', r'_b) we have C(b + b', r_b + r'_b) = C(b, r_b) + C(b', r'_b).
 *   T6. vcom_commit binds: changing a single bit of b yields a different
 *       commitment with overwhelming probability (sanity, not a security
 *       proof — the formal binding reduces to MSIS, see G1 + G2 v2 §3).
 *   T7. chknorm correctly accepts in-bound and rejects out-of-bound polys.
 */

#include <dap_common.h>
#include <dap_enc.h>
#include <dap_test.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "chipmunk/chipmunk_mring_statement.h"
#include "chipmunk/chipmunk_poly.h"

#define LOG_TAG "test_chipmunk_mring_vcom"

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

static void s_fill_ring_hash(uint8_t a_out[32], uint8_t a_byte)
{
    for (size_t i = 0u; i < 32u; ++i) {
        a_out[i] = (uint8_t)(a_byte ^ (uint8_t)i);
    }
}

static bool s_polys_equal(const chipmunk_poly_t *a, const chipmunk_poly_t *b)
{
    for (size_t i = 0u; i < CHIPMUNK_N; ++i) {
        if (a->coeffs[i] != b->coeffs[i]) {
            return false;
        }
    }
    return true;
}

static bool s_poly_is_zero(const chipmunk_poly_t *a)
{
    for (size_t i = 0u; i < CHIPMUNK_N; ++i) {
        if (a->coeffs[i] != 0) {
            return false;
        }
    }
    return true;
}

static void s_random_short_r_b(chipmunk_poly_t a_r_b[CHIPMUNK_MRING_K_PK],
                               const uint8_t a_seed[32])
{
    /* Deterministic short ternary-ish lane via chipmunk_lrs sampler. */
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        /*
         * chipmunk_lrs_h_to_short_poly is implemented for the C0 profile
         * (the LRS params_id is the only one its rejection sampler
         * accepts).  Reusing the underlying sampler is fine for MRNG
         * test fixtures — what we need is a deterministic bounded poly;
         * the on-the-wire domain separation for production sampling
         * will live inside chipmunk_mring_statement.c when the prover
         * path is implemented in M3.2+.
         */
        const int rc =
            chipmunk_lrs_h_to_short_poly(&a_r_b[j],
                                         "mring-test-rb",
                                         CHIPMUNK_LRS_PARAMS_C0,
                                         a_seed,
                                         /*index=*/j,
                                         /*bound=*/CHIPMUNK_MRING_BETA_W);
        dap_assert(rc == 0, "MRNG vcom test: r_b sampling must succeed");
    }
}

/* -------------------------------------------------------------------------
 * T1 + T2 + T3 — generators determinism, non-trivial, distinct
 * ---------------------------------------------------------------------- */

static bool s_test_generators_determinism_and_independence(void)
{
    uint8_t ring_hash_a[32], ring_hash_b[32];
    s_fill_ring_hash(ring_hash_a, 0xA1u);
    s_fill_ring_hash(ring_hash_b, 0xB7u);

    chipmunk_mring_vcom_gens_t gens_a1, gens_a2, gens_b;
    dap_assert(chipmunk_mring_derive_vcom_generators(&gens_a1, ring_hash_a) == 0,
               "derive_vcom_generators(ring_hash_a) must succeed");
    dap_assert(chipmunk_mring_derive_vcom_generators(&gens_a2, ring_hash_a) == 0,
               "derive_vcom_generators(ring_hash_a) re-run must succeed");
    dap_assert(chipmunk_mring_derive_vcom_generators(&gens_b, ring_hash_b) == 0,
               "derive_vcom_generators(ring_hash_b) must succeed");

    /* T1 — determinism. */
    dap_assert(s_polys_equal(&gens_a1.a, &gens_a2.a),
               "Determinism: generator `a` must be identical across runs");
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        dap_assert(s_polys_equal(&gens_a1.H_prime[j], &gens_a2.H_prime[j]),
                   "Determinism: H'_j must be identical across runs");
    }

    /* T2 — non-trivial (uniform sampler must produce non-zero poly). */
    dap_assert(!s_poly_is_zero(&gens_a1.a),
               "Generator `a` must not be the zero polynomial");
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        dap_assert(!s_poly_is_zero(&gens_a1.H_prime[j]),
                   "H'_j must not be the zero polynomial");
    }

    /* T3 — distinct seeds yield distinct generators (domain separation). */
    dap_assert(!s_polys_equal(&gens_a1.a, &gens_b.a),
               "Distinct ring_hash values must yield distinct `a`");
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        dap_assert(!s_polys_equal(&gens_a1.H_prime[j], &gens_b.H_prime[j]),
                   "Distinct ring_hash values must yield distinct H'_j");
    }

    /* T3' — slots within a single ring_hash MUST also differ
     * (independent domain separation: nonces 0..K_PK). */
    dap_assert(!s_polys_equal(&gens_a1.a, &gens_a1.H_prime[0]),
               "Generator `a` must differ from H'_0 (slot separation)");
    for (uint32_t j = 1u; j < CHIPMUNK_MRING_K_PK; ++j) {
        dap_assert(!s_polys_equal(&gens_a1.H_prime[0], &gens_a1.H_prime[j]),
                   "All H'_j slots must be pairwise distinct");
    }
    return true;
}

/* -------------------------------------------------------------------------
 * T4 — pack_b validation
 * ---------------------------------------------------------------------- */

static bool s_test_pack_b_validation(void)
{
    chipmunk_poly_t b_poly;

    /* Valid bit vector → success, low slots match, high slots zero. */
    uint8_t indicator_ok[16] = { 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1 };
    dap_assert(chipmunk_mring_vcom_pack_b(&b_poly, indicator_ok, 16u) == 0,
               "pack_b must accept a valid bit vector");
    for (uint32_t i = 0u; i < 16u; ++i) {
        dap_assert(b_poly.coeffs[i] == (int32_t)indicator_ok[i],
                   "pack_b: low coefficients must match indicator");
    }
    for (uint32_t i = 16u; i < CHIPMUNK_N; ++i) {
        dap_assert(b_poly.coeffs[i] == 0,
                   "pack_b: high coefficients must be zero");
    }

    /* Non-bit value (>= 2) → -EINVAL. */
    uint8_t indicator_bad[3] = { 0, 2, 1 };
    dap_assert(chipmunk_mring_vcom_pack_b(&b_poly, indicator_bad, 3u) == -EINVAL,
               "pack_b must reject non-bit values with -EINVAL");

    /* Out-of-range N → -EINVAL. */
    uint8_t one = 1u;
    dap_assert(chipmunk_mring_vcom_pack_b(&b_poly, &one, 1u) == -EINVAL,
               "pack_b must reject N < N_MIN");
    dap_assert(chipmunk_mring_vcom_pack_b(&b_poly, &one,
                                          CHIPMUNK_MRING_N_MAX + 1u) == -EINVAL,
               "pack_b must reject N > N_MAX");
    return true;
}

/* -------------------------------------------------------------------------
 * T5 — vcom homomorphism (linearity in b and r_b together)
 * ---------------------------------------------------------------------- */

static bool s_test_vcom_homomorphism(void)
{
    uint8_t ring_hash[32];
    s_fill_ring_hash(ring_hash, 0x42u);

    chipmunk_mring_vcom_gens_t gens;
    dap_assert(chipmunk_mring_derive_vcom_generators(&gens, ring_hash) == 0,
               "homomorphism: derive_vcom_generators must succeed");

    /* Two disjoint bit vectors so b + b' is still 0/1-valued
     * (this keeps the test inside pack_b's contract). */
    const uint32_t N = 8u;
    uint8_t b1[8]  = { 1, 0, 1, 0, 0, 1, 0, 0 };
    uint8_t b2[8]  = { 0, 1, 0, 1, 1, 0, 0, 1 };
    uint8_t bSum[8];
    for (uint32_t i = 0u; i < N; ++i) {
        bSum[i] = (uint8_t)(b1[i] + b2[i]);
    }

    chipmunk_poly_t bp1, bp2, bpSum;
    dap_assert(chipmunk_mring_vcom_pack_b(&bp1,  b1,   N) == 0, "pack b1");
    dap_assert(chipmunk_mring_vcom_pack_b(&bp2,  b2,   N) == 0, "pack b2");
    dap_assert(chipmunk_mring_vcom_pack_b(&bpSum, bSum, N) == 0, "pack bSum");

    uint8_t seed1[32] = { 0 }; seed1[0] = 0x11u;
    uint8_t seed2[32] = { 0 }; seed2[0] = 0x22u;
    chipmunk_poly_t rb1[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t rb2[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t rbSum[CHIPMUNK_MRING_K_PK];
    s_random_short_r_b(rb1, seed1);
    s_random_short_r_b(rb2, seed2);
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        dap_assert(chipmunk_poly_add_q(&rbSum[j], &rb1[j], &rb2[j], (uint64_t)CHIPMUNK_Q) == 0,
                   "rbSum poly_add must succeed");
    }

    chipmunk_poly_t C1, C2, CSum, CExpected;
    dap_assert(chipmunk_mring_vcom_commit(&C1,        &gens, &bp1, rb1, (uint64_t)CHIPMUNK_Q) == 0,
               "vcom_commit(b1,rb1)");;
    dap_assert(chipmunk_mring_vcom_commit(&C2,        &gens, &bp2, rb2, (uint64_t)CHIPMUNK_Q) == 0,
               "vcom_commit(b2,rb2)");;
    dap_assert(chipmunk_mring_vcom_commit(&CSum,      &gens, &bpSum, rbSum, (uint64_t)CHIPMUNK_Q) == 0,
               "vcom_commit(b1+b2, rb1+rb2)");;
    dap_assert(chipmunk_poly_add_q(&CExpected, &C1, &C2, (uint64_t)CHIPMUNK_Q) == 0,
               "C1+C2 add");;

    dap_assert(s_polys_equal(&CSum, &CExpected),
               "vcom must be homomorphic: C(b1+b2, rb1+rb2) == C(b1,rb1)+C(b2,rb2)");
    return true;
}

/* -------------------------------------------------------------------------
 * T6 — single-bit flip sanity (changing b changes C_b)
 * ---------------------------------------------------------------------- */

static bool s_test_vcom_bit_flip(void)
{
    uint8_t ring_hash[32];
    s_fill_ring_hash(ring_hash, 0x7Fu);

    chipmunk_mring_vcom_gens_t gens;
    dap_assert(chipmunk_mring_derive_vcom_generators(&gens, ring_hash) == 0,
               "bit-flip: derive_vcom_generators must succeed");

    const uint32_t N = 16u;
    uint8_t b[16] = { 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1 };
    uint8_t b_flipped[16];
    memcpy(b_flipped, b, sizeof(b));
    b_flipped[3] ^= 1u;  /* flip a single bit */

    chipmunk_poly_t bp, bp_flipped;
    dap_assert(chipmunk_mring_vcom_pack_b(&bp,         b,         N) == 0, "pack b");
    dap_assert(chipmunk_mring_vcom_pack_b(&bp_flipped, b_flipped, N) == 0, "pack b'");

    uint8_t seed[32] = { 0 }; seed[0] = 0xC3u;
    chipmunk_poly_t rb[CHIPMUNK_MRING_K_PK];
    s_random_short_r_b(rb, seed);

    chipmunk_poly_t C, C_flipped;
    dap_assert(chipmunk_mring_vcom_commit(&C,         &gens, &bp,         rb, (uint64_t)CHIPMUNK_Q) == 0,
               "vcom_commit(b)");
    dap_assert(chipmunk_mring_vcom_commit(&C_flipped, &gens, &bp_flipped, rb, (uint64_t)CHIPMUNK_Q) == 0,
               "vcom_commit(b')");

    dap_assert(!s_polys_equal(&C, &C_flipped),
               "A single-bit flip in b MUST yield a different commitment");
    return true;
}

/* -------------------------------------------------------------------------
 * T7 — norm check
 * ---------------------------------------------------------------------- */

static bool s_test_chknorm(void)
{
    chipmunk_poly_t p;
    memset(&p, 0, sizeof(p));
    p.coeffs[0] = 13;
    p.coeffs[5] = -13;
    p.coeffs[42] = 7;

    dap_assert(chipmunk_mring_chknorm(&p, 13, (uint64_t)CHIPMUNK_Q) == 0,
               "chknorm accepts |coeff| ≤ bound");
    dap_assert(chipmunk_mring_chknorm(&p, 12, (uint64_t)CHIPMUNK_Q) == -ERANGE,
               "chknorm rejects |coeff| > bound with -ERANGE");

    /* Boundary: coeff == -bound should be accepted (centered interval). */
    chipmunk_poly_t p_lo;
    memset(&p_lo, 0, sizeof(p_lo));
    p_lo.coeffs[0] = -13;
    dap_assert(chipmunk_mring_chknorm(&p_lo, 13, (uint64_t)CHIPMUNK_Q) == 0,
               "chknorm accepts coeff == -bound (centered interval)");

    dap_assert(chipmunk_mring_chknorm(NULL, 13, (uint64_t)CHIPMUNK_Q) == -EINVAL,
               "chknorm rejects NULL with -EINVAL");
    dap_assert(chipmunk_mring_chknorm(&p, -1, (uint64_t)CHIPMUNK_Q) == -EINVAL,
               "chknorm rejects negative bound with -EINVAL");
    return true;
}

int main(void)
{
    dap_set_appname("test_chipmunk_mring_statement_vcom");
    dap_common_init("test_chipmunk_mring_statement_vcom", NULL);
    /* Initialise crypto subsystem (SIMD dispatch, chipmunk, etc.) */
    dap_enc_init();

    int rc = 0;
    if (!s_test_generators_determinism_and_independence()) rc = 1;
    if (!s_test_pack_b_validation())                       rc = 1;
    if (!s_test_vcom_homomorphism())                       rc = 1;
    if (!s_test_vcom_bit_flip())                           rc = 1;
    if (!s_test_chknorm())                                 rc = 1;

    if (rc == 0) {
        log_it(L_INFO, "MRNG M3.1 statement-layer VCom tests PASSED");
    }
    dap_common_deinit();
    return rc;
}
