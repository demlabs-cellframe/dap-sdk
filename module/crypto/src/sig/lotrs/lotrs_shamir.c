/*
 * LoTRS — Shamir secret sharing over R_q.
 */

#include "lotrs_shamir.h"
#include "lotrs_sample.h"

#include <errno.h>
#include <string.h>

#define LOG_TAG "lotrs_shamir"
#include "dap_common.h"

/* Modular inverse via extended Euclidean algorithm. */
static int64_t s_modinv(int64_t a, int64_t m)
{
    int64_t old_r = a, r = m;
    int64_t old_s = 1, s = 0;
    while (r != 0) {
        int64_t q = old_r / r;
        int64_t tmp;
        tmp = r; r = old_r - q * r; old_r = tmp;
        tmp = s; s = old_s - q * s; old_s = tmp;
    }
    if (old_s < 0) old_s += m;
    return old_s;
}

/* Evaluate polynomial at point x using Horner's method: f(x) = c0 + x*(c1 + x*(c2 + ...)) */
static void s_poly_eval(uint64_t *a_out, const uint64_t *a_coeffs,
                        uint32_t a_d, uint64_t a_x, uint64_t a_q)
{
    uint64_t l_result = 0u;
    for (int32_t i = (int32_t)a_d - 1; i >= 0; --i) {
        __uint128_t l_prod = (__uint128_t)l_result * a_x;
        l_result = (uint64_t)(l_prod % a_q);
        l_result = (l_result + a_coeffs[i]) % a_q;
    }
    *a_out = l_result;
}

int lotrs_shamir_split(lotrs_poly_t **a_shares,
                       const lotrs_poly_t *a_secret,
                       uint32_t a_N, uint32_t a_T,
                       const lotrs_params_t *a_par,
                       void *a_xof)
{
    if (!a_shares || !a_secret || !a_par || !a_xof) return -EINVAL;
    if (a_T < 1u || a_T > a_N || a_N > 255u) return -EINVAL;

    const uint32_t l_d = a_par->d;
    const uint64_t l_q = a_par->q;

    /* Allocate shares. */
    for (uint32_t i = 0u; i < a_N; ++i) {
        a_shares[i] = lotrs_poly_alloc(a_par);
        if (!a_shares[i]) {
            for (uint32_t j = 0u; j < i; ++j) lotrs_poly_free(a_shares[j]);
            return -ENOMEM;
        }
    }

    /* Generate random coefficients for f(x) = secret + a_1*x + ... + a_{T-1}*x^{T-1}. */
    lotrs_poly_t **l_coeffs = DAP_NEW_Z_COUNT(lotrs_poly_t *, a_T);
    if (!l_coeffs) {
        for (uint32_t i = 0u; i < a_N; ++i) lotrs_poly_free(a_shares[i]);
        return -ENOMEM;
    }
    for (uint32_t t = 0u; t < a_T; ++t) {
        l_coeffs[t] = lotrs_poly_alloc(a_par);
        if (!l_coeffs[t]) {
            for (uint32_t j = 0u; j < t; ++j) lotrs_poly_free(l_coeffs[j]);
            DAP_DELETE(l_coeffs);
            for (uint32_t i = 0u; i < a_N; ++i) lotrs_poly_free(a_shares[i]);
            return -ENOMEM;
        }
    }

    /* f(0) = secret. */
    lotrs_poly_copy(l_coeffs[0], a_secret, a_par);

    /* f(1)..f(T-1) = random polynomials (d coefficients each). */
    for (uint32_t t = 1u; t < a_T; ++t) {
        int rc = lotrs_sample_uniform(l_coeffs[t], a_xof, a_par);
        if (rc != 0) {
            for (uint32_t j = 0u; j < a_T; ++j) lotrs_poly_free(l_coeffs[j]);
            DAP_DELETE(l_coeffs);
            for (uint32_t i = 0u; i < a_N; ++i) lotrs_poly_free(a_shares[i]);
            return rc;
        }
    }

    /* Evaluate f at points 1..N. */
    for (uint32_t i = 0u; i < a_N; ++i) {
        uint64_t l_x = (uint64_t)(i + 1u); /* 1-based indices */
        for (uint32_t k = 0u; k < l_d; ++k) {
            /* Evaluate polynomial at x for coefficient k. */
            uint64_t l_eval_coeffs[a_T];
            for (uint32_t t = 0u; t < a_T; ++t) {
                l_eval_coeffs[t] = l_coeffs[t]->coeffs[k];
            }
            uint64_t l_val = 0u;
            s_poly_eval(&l_val, l_eval_coeffs, a_T, l_x, l_q);
            a_shares[i]->coeffs[k] = l_val;
        }
    }

    for (uint32_t t = 0u; t < a_T; ++t) {
        lotrs_poly_free(l_coeffs[t]);
    }
    DAP_DELETE(l_coeffs);
    return 0;
}

int lotrs_shamir_reconstruct(lotrs_poly_t *a_out,
                             const lotrs_poly_t *const *a_shares,
                             const uint32_t *a_indices,
                             uint32_t a_T,
                             const lotrs_params_t *a_par)
{
    if (!a_out || !a_shares || !a_indices || !a_par) return -EINVAL;
    if (a_T < 1u) return -EINVAL;

    const uint32_t l_d = a_par->d;
    const int64_t l_q = (int64_t)a_par->q;

    lotrs_poly_zero(a_out, a_par);

    /* Lagrange interpolation: f(0) = Σ_i s_i * λ_i where λ_i = Π_{j≠i} (-j)/(i-j).
     * a_indices[i] is the 1-based index, a_shares[a_indices[i]-1] is the share. */
    for (uint32_t i = 0u; i < a_T; ++i) {
        uint32_t l_idx = a_indices[i]; /* 1-based */
        int64_t l_num = 1;
        int64_t l_den = 1;

        for (uint32_t j = 0u; j < a_T; ++j) {
            if (i == j) continue;
            uint32_t l_jdx = a_indices[j]; /* 1-based */
            l_num = (l_num * (-(int64_t)l_jdx)) % l_q;
            if (l_num < 0) l_num += l_q;
            l_den = (l_den * ((int64_t)l_idx - (int64_t)l_jdx)) % l_q;
            if (l_den < 0) l_den += l_q;
        }

        int64_t l_lambda = (l_num * s_modinv(l_den, l_q)) % l_q;
        if (l_lambda < 0) l_lambda += l_q;

        for (uint32_t k = 0u; k < l_d; ++k) {
            __uint128_t l_prod = (__uint128_t)a_shares[l_idx - 1u]->coeffs[k] * (uint64_t)l_lambda;
            uint64_t l_term = (uint64_t)(l_prod % (uint64_t)l_q);
            a_out->coeffs[k] = (a_out->coeffs[k] + l_term) % (uint64_t)l_q;
        }
    }

    return 0;
}
