/*
 * chipmunk_deep.c — DEEP composition polynomial for FRI-DEEP PCS.
 *
 * Implements the DEEP technique: for each committed polynomial f_i,
 * compute the DEEP quotient deep_q_i(X) = [f_i(X) - f_i(z)] / (X - z)
 * and combine into a single composition polynomial H(X) = Σ γ_i · deep_q_i(X).
 */

#include "chipmunk_deep.h"

#include <string.h>
#include <stdlib.h>

#include "dap_common.h"
#include "chipmunk_poly.h"
#include "chipmunk_field.h"

/* -------------------------------------------------------------------------
 * Horner evaluation of a degree-511 polynomial over F_q at a scalar point.
 * Returns f(x) mod q.
 * ------------------------------------------------------------------------- */
static int32_t s_poly_eval_fq(const int32_t coeffs[CHIPMUNK_N], int32_t x)
{
    int32_t result = 0;
    for (int i = CHIPMUNK_N - 1; i >= 0; --i) {
        int64_t val = (int64_t)x * (int64_t)result + (int64_t)coeffs[i];
        result = chipmunk_mod_q(val);
    }
    return result;
}

/* -------------------------------------------------------------------------
 * Synthetic division: given f(X) with f(z) = 0, compute q(X) = f(X) / (X-z).
 *
 * Uses Horner-like recurrence:
 *   q[N-2] = f[N-1]
 *   q[i] = f[i+1] + z * q[i+1]   for i = N-3 down to 0
 *   q[N-1] = 0  (quotient degree is N-2 at most)
 *
 * If f(z) != 0, we compute the "shifted" quotient:
 *   q(X) = [f(X) - f(z)] / (X - z)
 * by first subtracting f(z) from f(X), then dividing.
 *
 * Since f(X) - f(z) vanishes at z by construction, division is exact.
 * ------------------------------------------------------------------------- */
static void s_synth_div(int32_t q_out[CHIPMUNK_N],
                        const int32_t f[CHIPMUNK_N],
                        int32_t f_at_z,
                        int32_t z)
{
    /* Subtract f(z) from the constant term to make f - f(z) vanish at z.
     * f(X) - f(z) = (X - z) * q(X), so we need f[0] - f(z) as the remainder
     * adjustment. Since (X-z) has root z, evaluating at z gives 0. */
    int32_t l_adj_f0 = chipmunk_mod_q((int64_t)f[0] - (int64_t)f_at_z);

    /* Synthetic division: process from highest coefficient down.
     * q[N-1] = 0 (quotient has degree at most N-2)
     * q[i] = f[i+1] + z * q[i+1] for i = N-2 down to 0
     * Then verify: remainder = q[0] + z * f_at_z_adj should be 0 */
    memset(q_out, 0, CHIPMUNK_N * sizeof(int32_t));

    int64_t l_acc = 0;
    for (int i = CHIPMUNK_N - 1; i >= 1; --i) {
        l_acc = chipmunk_mod_q((int64_t)f[i] + (int64_t)z * l_acc);
        q_out[i - 1] = (int32_t)l_acc;
    }
    /* Now l_acc holds what would be the remainder = f[0] + z * l_acc.
     * We need to account for the f(z) subtraction: remainder should be l_adj_f0. */
    l_acc = chipmunk_mod_q((int64_t)l_adj_f0 + (int64_t)z * l_acc);

    /* l_acc should be 0 mod q if f(X) - f(z) = (X-z)*q(X).
     * For valid inputs this is guaranteed by construction. */
    (void)l_acc;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

int chipmunk_deep_prover_init(chipmunk_deep_prover_t *prov)
{
    if (!prov)
        return -1;

    memset(prov, 0, sizeof(*prov));
    return 0;
}

void chipmunk_deep_prover_free(chipmunk_deep_prover_t *prov)
{
    (void)prov;
    /* No dynamic allocation — no-op. */
}

int chipmunk_deep_compose(chipmunk_deep_prover_t *prov,
                          const chipmunk_poly_t polys[],
                          uint32_t num_polys,
                          int32_t z_point,
                          const int32_t gammas[])
{
    if (!prov || !polys || !gammas)
        return -1;
    if (num_polys == 0 || num_polys > CHIPMUNK_DEEP_MAX_POLYS)
        return -1;
    if (z_point == 0)
        return -1;

    prov->z = z_point;
    prov->num_polys = num_polys;
    memset(prov->composition, 0, sizeof(prov->composition));

    for (uint32_t p = 0; p < num_polys; ++p) {
        prov->gammas[p] = gammas[p];

        /* Step 1: Evaluate f_p(z). */
        prov->evals[p] = s_poly_eval_fq(polys[p].coeffs, z_point);

        /* Step 2: Synthetic division: q_p(X) = [f_p(X) - f_p(z)] / (X - z). */
        int32_t l_quotient[CHIPMUNK_N];
        s_synth_div(l_quotient, polys[p].coeffs, prov->evals[p], z_point);

        /* Step 3: Accumulate: H(X) += gamma_p * q_p(X). */
        for (unsigned i = 0; i < CHIPMUNK_N; ++i) {
            int64_t l_term = (int64_t)gammas[p] * (int64_t)l_quotient[i];
            prov->composition[i] = chipmunk_mod_q(
                (int64_t)prov->composition[i] + l_term);
        }
    }

    prov->composed = true;
    return 0;
}

bool chipmunk_deep_verify(const chipmunk_deep_opening_t *opening,
                          const chipmunk_poly_t polys[],
                          uint32_t num_polys,
                          const int32_t gammas[],
                          const int32_t composition[CHIPMUNK_N],
                          int32_t x)
{
    if (!opening || !polys || !gammas || !composition)
        return false;
    if (num_polys == 0 || num_polys > CHIPMUNK_DEEP_MAX_POLYS)
        return false;
    if (opening->num_polys != num_polys)
        return false;
    if (x == opening->z_point)
        return false;

    int32_t l_z = opening->z_point;
    int32_t l_inv_xz = chipmunk_field_inv(
        chipmunk_mod_q((int64_t)x - (int64_t)l_z));

    /* H(x) via Horner from committed composition polynomial. */
    int32_t l_h_at_x = s_poly_eval_fq(composition, x);

    /* RHS: Σ γ_i · [f_i(x) - f_i(z)] / (x - z) */
    int64_t l_rhs = 0;
    for (uint32_t p = 0; p < num_polys; ++p) {
        int32_t l_fi_at_x = s_poly_eval_fq(polys[p].coeffs, x);
        int32_t l_diff = chipmunk_mod_q(
            (int64_t)l_fi_at_x - (int64_t)opening->evals[p]);
        int64_t l_term = (int64_t)gammas[p] * (int64_t)chipmunk_mod_q(
            (int64_t)l_diff * (int64_t)l_inv_xz);
        l_rhs = chipmunk_mod_q(l_rhs + l_term);
    }

    return l_h_at_x == (int32_t)l_rhs;
}

const int32_t *chipmunk_deep_prover_composition(const chipmunk_deep_prover_t *prov)
{
    if (!prov || !prov->composed)
        return NULL;
    return prov->composition;
}

int chipmunk_deep_build_opening(chipmunk_deep_opening_t *opening,
                               const chipmunk_deep_prover_t *prov)
{
    if (!opening || !prov || !prov->composed)
        return -1;

    opening->z_point = prov->z;
    opening->num_polys = prov->num_polys;
    memcpy(opening->evals, prov->evals,
           prov->num_polys * sizeof(int32_t));
    return 0;
}
