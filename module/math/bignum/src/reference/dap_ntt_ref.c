/**
 * @file dap_ntt_ref.c
 * @brief Reference NTT implementation — portable C, parameterized by dap_ntt_params_t / dap_ntt_params16_t.
 *
 * 32-bit API: Chipmunk table-driven CT/GS + Dilithium Montgomery-domain CT/GS.
 * 16-bit API: Kyber-style Montgomery-domain CT/GS with R=2^16.
 *
 * @authors naeper
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_ntt.h"
#include <string.h>

/* ===== 32-bit API ===== */

/**
 * @brief Forward NTT — Cooley–Tukey butterfly with table-driven twiddle factors.
 *
 * Matches the Chipmunk Rust algorithm structure:
 *   t = n; for l in 0..log2(n): m = 1<<l; ht = t>>1; ...
 */
void dap_ntt_forward_ref(int32_t *a_coeffs, const dap_ntt_params_t *a_params)
{
    const int32_t l_q = a_params->q;
    const int32_t *l_fwd = a_params->zetas;
    int l_t = (int)a_params->n;
    int l_log_n = 0;
    { int l_tmp = l_t; while (l_tmp > 1) { l_log_n++; l_tmp >>= 1; } }

    for (int l = 0; l < l_log_n; l++) {
        int l_m  = 1 << l;
        int l_ht = l_t >> 1;
        int l_i  = 0, l_j1 = 0;

        while (l_i < l_m) {
            int32_t l_s = l_fwd[l_m + l_i];
            int l_j2 = l_j1 + l_ht;

            for (int l_j = l_j1; l_j < l_j2; l_j++) {
                int32_t l_u = a_coeffs[l_j];
                int32_t l_v = (int32_t)(((int64_t)a_coeffs[l_j + l_ht] * l_s) % l_q);
                a_coeffs[l_j]        = (l_u + l_v) % l_q;
                a_coeffs[l_j + l_ht] = (l_u + l_q - l_v) % l_q;
            }
            l_i++;
            l_j1 += l_t;
        }
        l_t = l_ht;
    }
}

/**
 * @brief Inverse NTT — Gentleman–Sande butterfly, followed by 1/n scaling.
 */
void dap_ntt_inverse_ref(int32_t *a_coeffs, const dap_ntt_params_t *a_params)
{
    const int32_t l_q = a_params->q;
    const int32_t *l_inv = a_params->zetas_inv;
    int l_t = 1;
    int l_m = (int)a_params->n;

    while (l_m > 1) {
        int l_hm = l_m >> 1;
        int l_dt = l_t << 1;
        int l_i  = 0, l_j1 = 0;

        while (l_i < l_hm) {
            int l_j2 = l_j1 + l_t;
            int32_t l_s = l_inv[l_hm + l_i];

            for (int l_j = l_j1; l_j < l_j2; l_j++) {
                int32_t l_u = a_coeffs[l_j];
                int32_t l_v = a_coeffs[l_j + l_t];
                a_coeffs[l_j]       = (l_u + l_v) % l_q;
                a_coeffs[l_j + l_t] = (int32_t)(((int64_t)(l_u + l_q - l_v) * l_s) % l_q);
            }
            l_i++;
            l_j1 += l_dt;
        }
        l_t = l_dt;
        l_m = l_hm;
    }

    int32_t l_inv_n = a_params->one_over_n;
    for (uint32_t i = 0; i < a_params->n; i++) {
        int64_t l_tmp = ((int64_t)a_coeffs[i] * l_inv_n) % l_q;
        a_coeffs[i] = (int32_t)l_tmp;
        if (a_coeffs[i] > l_q / 2)  a_coeffs[i] -= l_q;
        if (a_coeffs[i] < -l_q / 2) a_coeffs[i] += l_q;
    }
}

void dap_ntt_pointwise_montgomery_ref(int32_t *a_c, const int32_t *a_a,
                                      const int32_t *a_b, const dap_ntt_params_t *a_params)
{
    for (uint32_t i = 0; i < a_params->n; i++) {
        int64_t l_product = (int64_t)a_a[i] * a_b[i];
        a_c[i] = dap_ntt_montgomery_reduce(l_product, a_params);
    }
}

/*
 * Raw Montgomery reduce: a * R^{-1} mod q, result in approximately (-q, q).
 * Same algorithm as dap_ntt_montgomery_reduce() but without the final
 * normalization to [0, q) — this is required for the NTT butterfly where
 * intermediate coefficients must stay centered around zero.
 */
static inline int32_t s_montgomery_reduce_raw(int64_t a_value,
                                              const dap_ntt_params_t *a_params)
{
    uint32_t l_mask = a_params->mont_r_mask;
    int64_t l_u = ((uint64_t)a_value & l_mask) * a_params->qinv;
    l_u &= l_mask;
    return (int32_t)((a_value + l_u * (int64_t)a_params->q) >> a_params->mont_r_bits);
}

/**
 * @brief Montgomery-domain forward NTT (Cooley–Tukey, sequential zeta walk).
 *
 * Matches the Dilithium NTT structure: len = n/2 down to 1, zetas[k++].
 * Uses raw Montgomery reduce to keep coefficients bounded in [-q, q)
 * throughout all layers (no +2q that causes overflow for large q).
 */
void dap_ntt_forward_mont_ref(int32_t *a_coeffs, const dap_ntt_params_t *a_params)
{
    const int32_t *l_zetas = a_params->zetas;
    unsigned int l_len, l_start, l_j, l_k = 1;

    for (l_len = a_params->n / 2; l_len >= 1; l_len >>= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            int32_t l_zeta = l_zetas[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int32_t l_t = s_montgomery_reduce_raw(
                        (int64_t)l_zeta * a_coeffs[l_j + l_len], a_params);
                a_coeffs[l_j + l_len] = a_coeffs[l_j] - l_t;
                a_coeffs[l_j]         = a_coeffs[l_j] + l_t;
            }
        }
    }
}

/**
 * @brief Montgomery-domain inverse NTT (Gentleman–Sande, sequential zeta walk).
 *
 * Final scaling by one_over_n is NOT applied here — caller must handle the
 * combined Montgomery + 1/n factor as appropriate for the algorithm.
 */
void dap_ntt_inverse_mont_ref(int32_t *a_coeffs, const dap_ntt_params_t *a_params)
{
    const int32_t *l_zinv = a_params->zetas_inv;
    unsigned int l_start, l_len, l_j, l_k = 0;

    for (l_len = 1; l_len < a_params->n; l_len <<= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            int32_t l_zeta = l_zinv[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int32_t l_t    = a_coeffs[l_j];
                a_coeffs[l_j]        = l_t + a_coeffs[l_j + l_len];
                a_coeffs[l_j + l_len] = s_montgomery_reduce_raw(
                        (int64_t)l_zeta * (l_t - a_coeffs[l_j + l_len]), a_params);
            }
        }
    }
}

/* ===== 16-bit API (Kyber-style, R = 2^16) ===== */

/**
 * @brief Forward NTT (Cooley–Tukey, sequential zeta walk, Montgomery butterfly).
 * Input in standard order, output in bit-reversed order. Loop: len = n/2 down to 2.
 */
void dap_ntt16_forward_ref(int16_t *a_coeffs, const dap_ntt_params16_t *a_params)
{
    const int16_t *l_z = a_params->zetas;
    unsigned int l_len, l_start, l_j, l_k = 1;

    for (l_len = a_params->n / 2; l_len >= 2; l_len >>= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            int16_t l_zeta = l_z[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int16_t l_t = dap_ntt16_fqmul(l_zeta, a_coeffs[l_j + l_len], a_params);
                a_coeffs[l_j + l_len] = a_coeffs[l_j] - l_t;
                a_coeffs[l_j]         = a_coeffs[l_j] + l_t;
            }
        }
    }
}

/**
 * @brief Inverse NTT (Gentleman–Sande, sequential zeta walk, Montgomery butterfly).
 * Input in bit-reversed order, output in standard order.
 * Final scaling by zetas_inv[zetas_len-1] (the combined 1/n * R factor).
 */
void dap_ntt16_inverse_ref(int16_t *a_coeffs, const dap_ntt_params16_t *a_params)
{
    const int16_t *l_zinv = a_params->zetas_inv;
    unsigned int l_start, l_len, l_j, l_k = 0;

    for (l_len = 2; l_len <= a_params->n / 2; l_len <<= 1) {
        for (l_start = 0; l_start < a_params->n; l_start = l_j + l_len) {
            int16_t l_zeta = l_zinv[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int16_t l_t    = a_coeffs[l_j];
                a_coeffs[l_j]          = dap_ntt16_barrett_reduce(l_t + a_coeffs[l_j + l_len], a_params);
                a_coeffs[l_j + l_len]  = l_t - a_coeffs[l_j + l_len];
                a_coeffs[l_j + l_len]  = dap_ntt16_fqmul(l_zeta, a_coeffs[l_j + l_len], a_params);
            }
        }
    }

    int16_t l_scale = l_zinv[a_params->zetas_len - 1];
    for (unsigned int i = 0; i < a_params->n; i++)
        a_coeffs[i] = dap_ntt16_fqmul(a_coeffs[i], l_scale, a_params);
}

void dap_ntt16_basemul_ref(int16_t a_r[2], const int16_t a_a[2], const int16_t a_b[2],
                           int16_t a_zeta, const dap_ntt_params16_t *a_params)
{
    a_r[0]  = dap_ntt16_fqmul(a_a[1], a_b[1], a_params);
    a_r[0]  = dap_ntt16_fqmul(a_r[0], a_zeta, a_params);
    a_r[0] += dap_ntt16_fqmul(a_a[0], a_b[0], a_params);

    a_r[1]  = dap_ntt16_fqmul(a_a[0], a_b[1], a_params);
    a_r[1] += dap_ntt16_fqmul(a_a[1], a_b[0], a_params);
}
