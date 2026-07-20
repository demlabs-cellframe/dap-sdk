/*
 * test_chipmunk_mring_statement_bind.c — MRNG M3.3 bind-block helpers.
 *
 * CR-11.G Phase 7.7 / task_ac273cea.  Validates G2 v2.1 §4 same-witness
 * binding primitives:
 *
 *   z_x = ρ_x + c*·X                       (prover, all in R_q^{K_pk})
 *   M_pk = A_pk · z_x  −  c* · Y_pk        (verifier reconstruction)
 *   M_T  = A_T  · z_x  −  c* · T           (verifier reconstruction)
 *
 * For honest (ρ_x, X), the verifier's M_pk must equal A_pk · ρ_x and
 * M_T must equal A_T · ρ_x (the algebra is tautological since
 * A_pk·z_x = A_pk·(ρ_x + c*·X) = A_pk·ρ_x + c*·Y_pk).  Any tampering
 * of z_x, Y_pk, T, A_pk or A_T breaks this identity with overwhelming
 * probability.
 *
 * Tested properties:
 *   T1. derive_A_T determinism + slot independence + per-(ring,ctx)
 *       domain separation.
 *   T2. bind_mask_sample determinism + norm bounded by MASK_BOUND.
 *   T3. SAME-WITNESS reconstruction identity for both legs (A_pk, A_T)
 *       on honest inputs, across multiple random c*.
 *   T4. Tampered z_x[j] in one coordinate corrupts M_pk reconstruction.
 *   T5. -ERANGE returned on verify when z_x violates norm bound.
 *   T6. bind_prove returns -EAGAIN when norm overflows (constructed by
 *       handing in an X with deliberately large entries — verifies the
 *       abort gate works without a real loop).
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
#include "chipmunk/chipmunk_lrs.h"

#define LOG_TAG "test_chipmunk_mring_bind"

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

/*
 * Compare two R_q polynomials for equality, normalising both into the
 * canonical positive residue [0, q) first.  This is necessary because
 * chipmunk_lrs_relation_eval emits canonical-positive coefficients
 * whereas our s_poly_mul_time / chipmunk_poly_sub composition leaves
 * results in the centred range (−q/2, q/2] — the two representations
 * denote the same R_q element but compare unequal byte-for-byte.
 */
static int32_t s_canon_mod_q(int32_t a_v)
{
    int64_t v = (int64_t)a_v % (int64_t)CHIPMUNK_Q;
    if (v < 0) v += (int64_t)CHIPMUNK_Q;
    return (int32_t)v;
}

static bool s_polys_equal(const chipmunk_poly_t *a, const chipmunk_poly_t *b)
{
    for (size_t i = 0u; i < CHIPMUNK_N; ++i) {
        if (s_canon_mod_q(a->coeffs[i]) != s_canon_mod_q(b->coeffs[i])) {
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

static void s_fill_hash(uint8_t a_out[32], uint8_t a_byte)
{
    for (size_t i = 0u; i < 32u; ++i) {
        a_out[i] = (uint8_t)(a_byte ^ (uint8_t)i);
    }
}

static void s_derive_X(chipmunk_poly_t a_X[CHIPMUNK_MRING_K_PK], uint8_t a_salt)
{
    uint8_t x_seed[CHIPMUNK_LRS_SEED_BYTES];
    for (size_t i = 0u; i < CHIPMUNK_LRS_SEED_BYTES; ++i) {
        x_seed[i] = (uint8_t)((i * 17u) ^ a_salt);
    }
    dap_assert(chipmunk_lrs_derive_witness(a_X, x_seed) == 0,
               "test fixture: derive X");
}

static void s_sample_c_star(chipmunk_poly_t *a_c, uint8_t a_salt)
{
    uint8_t seed[CHIPMUNK_LRS_CHALLENGE_SEED_BYTES];
    for (size_t i = 0u; i < CHIPMUNK_LRS_CHALLENGE_SEED_BYTES; ++i) {
        seed[i] = (uint8_t)(0x5Au ^ (uint8_t)i ^ a_salt);
    }
    dap_assert(chipmunk_lrs_h_to_sparse_ternary(a_c,
                                                "mring-test-c*",
                                                CHIPMUNK_LRS_PARAMS_C0,
                                                seed) == 0,
               "test fixture: sample c*");
}

/* -------------------------------------------------------------------------
 * T1 — derive_A_T determinism + independence
 * ---------------------------------------------------------------------- */

static bool s_test_derive_A_T(void)
{
    uint8_t ring_a[32], ring_b[32], ctx_a[32], ctx_b[32];
    s_fill_hash(ring_a, 0x11);
    s_fill_hash(ring_b, 0x22);
    s_fill_hash(ctx_a,  0x33);
    s_fill_hash(ctx_b,  0x44);

    chipmunk_poly_t AT_ra_ca_1[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t AT_ra_ca_2[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t AT_rb_ca  [CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t AT_ra_cb  [CHIPMUNK_MRING_K_PK];

    dap_assert(chipmunk_mring_derive_A_T(AT_ra_ca_1, ring_a, ctx_a) == 0, "AT(ra,ca)");
    dap_assert(chipmunk_mring_derive_A_T(AT_ra_ca_2, ring_a, ctx_a) == 0, "AT(ra,ca) #2");
    dap_assert(chipmunk_mring_derive_A_T(AT_rb_ca,   ring_b, ctx_a) == 0, "AT(rb,ca)");
    dap_assert(chipmunk_mring_derive_A_T(AT_ra_cb,   ring_a, ctx_b) == 0, "AT(ra,cb)");

    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        dap_assert(s_polys_equal(&AT_ra_ca_1[j], &AT_ra_ca_2[j]),
                   "derive_A_T determinism: same (ring,ctx) yields same slot j");
        dap_assert(!s_poly_is_zero(&AT_ra_ca_1[j]),
                   "derive_A_T: slot must be non-trivial");
    }
    /* Different ring_hash → different A_T (at least one slot differs). */
    bool ring_sep = false;
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        if (!s_polys_equal(&AT_ra_ca_1[j], &AT_rb_ca[j])) { ring_sep = true; break; }
    }
    dap_assert(ring_sep, "derive_A_T: changing ring_hash must change A_T");
    /* Different ctx_hash → different A_T. */
    bool ctx_sep = false;
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        if (!s_polys_equal(&AT_ra_ca_1[j], &AT_ra_cb[j])) { ctx_sep = true; break; }
    }
    dap_assert(ctx_sep, "derive_A_T: changing ctx_hash must change A_T");
    /* Slot independence within one A_T. */
    for (uint32_t j = 1u; j < CHIPMUNK_MRING_K_PK; ++j) {
        dap_assert(!s_polys_equal(&AT_ra_ca_1[0], &AT_ra_ca_1[j]),
                   "derive_A_T: slots within one A_T must be pairwise distinct");
    }
    return true;
}

/* -------------------------------------------------------------------------
 * T2 — mask sample determinism + norm bound
 * ---------------------------------------------------------------------- */

static bool s_test_mask_sample(void)
{
    uint8_t seed[32];
    s_fill_hash(seed, 0x77);

    chipmunk_poly_t rho_a[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t rho_a2[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t rho_b[CHIPMUNK_MRING_K_PK];

    dap_assert(chipmunk_mring_bind_mask_sample(rho_a,  seed, /*attempt=*/0u) == 0, "mask #0");
    dap_assert(chipmunk_mring_bind_mask_sample(rho_a2, seed, /*attempt=*/0u) == 0, "mask #0 again");
    dap_assert(chipmunk_mring_bind_mask_sample(rho_b,  seed, /*attempt=*/1u) == 0, "mask #1");

    /* Determinism + per-attempt domain separation + norm bound. */
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        dap_assert(s_polys_equal(&rho_a[j], &rho_a2[j]),
                   "mask_sample determinism: same (seed, attempt) yields same slot");
        dap_assert(!s_polys_equal(&rho_a[j], &rho_b[j]),
                   "mask_sample: different attempt must yield different mask");
        dap_assert(chipmunk_lrs_poly_chknorm_centered(
                       &rho_a[j], CHIPMUNK_MRING_MASK_BOUND, (uint64_t)CHIPMUNK_Q) == 0,
                   "mask_sample: ‖ρ_x[j]‖∞ ≤ MASK_BOUND");
    }
    return true;
}

/* -------------------------------------------------------------------------
 * T3 + T4 — same-witness reconstruction + tamper sanity
 * ---------------------------------------------------------------------- */

static bool s_run_bind_round(uint8_t a_salt,
                             bool a_tamper_z_x,
                             bool a_expect_pass)
{
    /* Setup. */
    chipmunk_poly_t A_pk[CHIPMUNK_MRING_K_PK];
    dap_assert(chipmunk_lrs_derive_A_pk(A_pk, CHIPMUNK_LRS_PARAMS_C0) == 0,
               "derive A_pk");

    uint8_t ring_hash[32], ctx_hash[32];
    s_fill_hash(ring_hash, 0xAB);
    s_fill_hash(ctx_hash,  0xCD);

    chipmunk_poly_t A_T[CHIPMUNK_MRING_K_PK];
    dap_assert(chipmunk_mring_derive_A_T(A_T, ring_hash, ctx_hash) == 0,
               "derive A_T");

    chipmunk_poly_t X[CHIPMUNK_MRING_K_PK];
    s_derive_X(X, a_salt);

    /* Y_pk = A_pk · X,  T = A_T · X. */
    chipmunk_poly_t Y_pk, T;
    dap_assert(chipmunk_lrs_relation_eval(&Y_pk, A_pk, X, (uint64_t)CHIPMUNK_Q) == 0, "Y_pk");
    dap_assert(chipmunk_lrs_relation_eval(&T,    A_T,  X, (uint64_t)CHIPMUNK_Q) == 0, "T");

    /* Sample mask ρ_x and the bind challenge c*. */
    uint8_t mask_seed[32];
    s_fill_hash(mask_seed, (uint8_t)(0x99 ^ a_salt));
    chipmunk_poly_t rho_x[CHIPMUNK_MRING_K_PK];
    dap_assert(chipmunk_mring_bind_mask_sample(rho_x, mask_seed, 0u) == 0,
               "ρ_x sample");

    chipmunk_poly_t c_star;
    s_sample_c_star(&c_star, a_salt);

    /*
     * Compute z_x; bounded-uniform abort means a single attempt accepts
     * only ~6% of the time (BETA/RESPONSE_BOUND ratio over n·K_pk = 3 072
     * coefficients), so the test loop walks attempts up to the LRS-class
     * MAX_ATTEMPTS cap.  In practice convergence happens within ≲ 50
     * attempts; the wide cap guards against unlucky seeds.
     */
    chipmunk_poly_t z_x[CHIPMUNK_MRING_K_PK];
    int rc_prove = -EAGAIN;
    for (uint32_t att = 0u;
         att < CHIPMUNK_MRING_MAX_ATTEMPTS && rc_prove == -EAGAIN;
         ++att) {
        if (att > 0u) {
            dap_assert(chipmunk_mring_bind_mask_sample(rho_x, mask_seed, att) == 0,
                       "ρ_x resample");
        }
        rc_prove = chipmunk_mring_bind_prove_z_x(z_x, rho_x, &c_star, X, (uint64_t)CHIPMUNK_Q);
    }
    dap_assert(rc_prove == 0,
               "bind_prove must converge within MAX_ATTEMPTS for honest X");

    if (a_tamper_z_x) {
        /* Bump one coefficient of one z_x[j] by 1; still within bound. */
        z_x[2].coeffs[7] = (z_x[2].coeffs[7] + 1) % CHIPMUNK_Q;
    }

    /* Verifier reconstructs M_pk, M_T. */
    chipmunk_poly_t M_pk, M_T;
    const int rc_verify =
        chipmunk_mring_bind_verify_reconstruct(&M_pk, &M_T,
                                               A_pk, A_T,
                                               z_x, &c_star,
                                               &Y_pk, &T, (uint64_t)CHIPMUNK_Q);
    dap_assert(rc_verify == 0,
               "bind_verify_reconstruct must succeed (norm in range)");

    /* Honest reconstruction: M_pk should equal A_pk · ρ_x, and M_T should
     * equal A_T · ρ_x.  If z_x is tampered, this must FAIL. */
    chipmunk_poly_t expected_M_pk, expected_M_T;
    dap_assert(chipmunk_lrs_relation_eval(&expected_M_pk, A_pk, rho_x, (uint64_t)CHIPMUNK_Q) == 0,
               "expected M_pk");
    dap_assert(chipmunk_lrs_relation_eval(&expected_M_T, A_T, rho_x, (uint64_t)CHIPMUNK_Q) == 0,
               "expected M_T");

    const bool match_pk = s_polys_equal(&M_pk, &expected_M_pk);
    const bool match_T  = s_polys_equal(&M_T,  &expected_M_T);
    const bool both_ok  = match_pk && match_T;

    if (a_expect_pass) {
        dap_assert(both_ok,
                   "Honest bind: M_pk = A_pk·ρ_x AND M_T = A_T·ρ_x");
        return both_ok;
    }
    /* Tampered: identity must FAIL. */
    dap_assert(!both_ok,
               "Tampered z_x: at least one of (M_pk, M_T) must diverge");
    return !both_ok;
}

static bool s_test_bind_honest_multiple(void)
{
    for (uint8_t salt = 1u; salt <= 4u; ++salt) {
        if (!s_run_bind_round(salt, /*tamper=*/false, /*expect_pass=*/true)) {
            return false;
        }
    }
    return true;
}

static bool s_test_bind_tampered(void)
{
    for (uint8_t salt = 5u; salt <= 8u; ++salt) {
        if (!s_run_bind_round(salt, /*tamper=*/true, /*expect_pass=*/false)) {
            return false;
        }
    }
    return true;
}

/* -------------------------------------------------------------------------
 * T5 — verify returns -ERANGE when z_x is out of bound
 * ---------------------------------------------------------------------- */

static bool s_test_verify_norm_gate(void)
{
    chipmunk_poly_t A_pk[CHIPMUNK_MRING_K_PK];
    dap_assert(chipmunk_lrs_derive_A_pk(A_pk, CHIPMUNK_LRS_PARAMS_C0) == 0, "A_pk");
    uint8_t ring_hash[32], ctx_hash[32];
    s_fill_hash(ring_hash, 0x10);
    s_fill_hash(ctx_hash,  0x20);
    chipmunk_poly_t A_T[CHIPMUNK_MRING_K_PK];
    dap_assert(chipmunk_mring_derive_A_T(A_T, ring_hash, ctx_hash) == 0, "A_T");

    chipmunk_poly_t z_x[CHIPMUNK_MRING_K_PK] = { 0 };
    /* Strictly out of bound: bound check uses the closed interval at
     * RESPONSE_BOUND, so any value > RESPONSE_BOUND must trip ERANGE. */
    z_x[0].coeffs[0] = CHIPMUNK_MRING_RESPONSE_BOUND + 1;

    chipmunk_poly_t c_star, Y_pk = { 0 }, T = { 0 };
    s_sample_c_star(&c_star, 0xEE);

    chipmunk_poly_t M_pk, M_T;
    const int rc =
        chipmunk_mring_bind_verify_reconstruct(&M_pk, &M_T,
                                               A_pk, A_T,
                                               z_x, &c_star,
                                               &Y_pk, &T, (uint64_t)CHIPMUNK_Q);
    dap_assert(rc == -ERANGE,
               "verify_reconstruct: out-of-range z_x must return -ERANGE");
    return true;
}

/* -------------------------------------------------------------------------
 * T6 — bind_prove returns -EAGAIN when the abort fires
 *
 * Synthesise a deliberately-large X whose c*·X swamps ρ_x.  We do this
 * by setting X to the unscaled all-ones constant polynomial (well above
 * the LRS witness bound), which produces a c*·X with large coefficients
 * after NTT multiplication and forces the norm check to reject.
 * ---------------------------------------------------------------------- */

static bool s_test_prove_abort_eagain(void)
{
    /* X with very large constant value in every slot. */
    chipmunk_poly_t X[CHIPMUNK_MRING_K_PK];
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        memset(&X[j], 0, sizeof(chipmunk_poly_t));
        for (size_t k = 0u; k < CHIPMUNK_N; ++k) {
            X[j].coeffs[k] = CHIPMUNK_MRING_RESPONSE_BOUND - 100;
        }
    }

    uint8_t mask_seed[32];
    s_fill_hash(mask_seed, 0xF0);
    chipmunk_poly_t rho_x[CHIPMUNK_MRING_K_PK];
    dap_assert(chipmunk_mring_bind_mask_sample(rho_x, mask_seed, 0u) == 0,
               "ρ_x sample");

    chipmunk_poly_t c_star;
    s_sample_c_star(&c_star, 0xBE);

    chipmunk_poly_t z_x[CHIPMUNK_MRING_K_PK];
    const int rc = chipmunk_mring_bind_prove_z_x(z_x, rho_x, &c_star, X, (uint64_t)CHIPMUNK_Q);
    dap_assert(rc == -EAGAIN,
               "bind_prove: oversized witness must trigger -EAGAIN abort");
    return true;
}

int main(void)
{
    dap_set_appname("test_chipmunk_mring_statement_bind");
    dap_common_init("test_chipmunk_mring_statement_bind", NULL);
    /* Initialise crypto subsystem (SIMD dispatch, chipmunk, etc.) */
    dap_enc_init();

    int rc = 0;
    if (!s_test_derive_A_T())             rc = 1;
    if (!s_test_mask_sample())            rc = 1;
    if (!s_test_bind_honest_multiple())   rc = 1;
    if (!s_test_bind_tampered())          rc = 1;
    if (!s_test_verify_norm_gate())       rc = 1;
    if (!s_test_prove_abort_eagain())     rc = 1;

    if (rc == 0) {
        log_it(L_INFO,
               "MRNG M3.3 bind-block tests PASSED "
               "(derive_A_T, mask_sample, same-witness binding, tamper "
               "soundness, norm gate, abort EAGAIN)");
    }
    dap_common_deinit();
    return rc;
}
