/*
 * chipmunk_ntt.c — 512-point NTT for chipmunk lattice cryptography.
 *
 * Uses dap_ntt Montgomery kernels (dap_ntt_forward_mont / dap_ntt_inverse_mont)
 * which are SIMD-dispatched (AVX2/AVX-512/NEON) when mont_r_bits == 32.
 *
 * Montgomery convention:
 *   R = 2^32.  Zeta tables store omega^{brv(i)} * R mod q.
 *   Forward:  standard [0,q) input → Montgomery-domain output (coeff * R mod q).
 *   Inverse:  Montgomery-domain input → standard centered [-q/2, q/2) output.
 *             dap_ntt_inverse_mont does NOT cancel R or apply 1/N; we do both
 *             in one Montgomery-multiply by one_over_n per coefficient.
 *
 * Pointwise mul in NTT domain uses dap_ntt_pointwise_montgomery (SIMD-dispatched),
 * which computes a*b*R^{-1} mod q, keeping the result in Montgomery domain.
 *
 * Global context: built lazily via pthread_once from chipmunk_ntt_params_compute,
 * eliminating all hardcoded twiddle tables. The same code path serves both the
 * default CHIPMUNK_Q and arbitrary per-q contexts.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "chipmunk.h"
#include "chipmunk_ntt.h"
#include "chipmunk_field.h"
#include "dap_ntt.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <inttypes.h>
#include "dap_common.h"
#include "dap_memwipe.h"

#define LOG_TAG "chipmunk_ntt"

/* -------------------------------------------------------------------------
 * Global NTT context — lazily built via pthread_once
 * ------------------------------------------------------------------------- */

static chipmunk_ntt_ctx_t s_global_ctx;
static pthread_once_t s_global_once = PTHREAD_ONCE_INIT;
static int s_global_init_result = 0;

static void s_global_init(void)
{
    s_global_init_result =
        chipmunk_ntt_params_compute(&s_global_ctx, (uint64_t)CHIPMUNK_Q);
    if (s_global_init_result != 0) {
        log_it(L_CRITICAL, "chipmunk_ntt: global ctx init FAILED for q=%d (rc=%d)",
               CHIPMUNK_Q, s_global_init_result);
    }
}

/** Ensure the global context is built. Returns 0 on success. */
static int s_global_ensure(void)
{
    pthread_once(&s_global_once, s_global_init);
    return s_global_init_result;
}

/* For external code that reads g_chipmunk_ntt_params (e.g. batch_verify). */
const dap_ntt_params_t *chipmunk_ntt_global_params(void)
{
    if (s_global_ensure() != 0) return NULL;
    return &s_global_ctx.params;
}

/* -------------------------------------------------------------------------
 * Extended Euclid: compute q^{-1} mod 2^k
 * ------------------------------------------------------------------------- */

static uint32_t s_modinv_pow2(uint64_t q, uint32_t k)
{
    uint64_t l_mod = 1ULL << k;
    uint64_t x = 1;
    for (uint32_t i = 1; i < k; i <<= 1) {
        x = (x * (2 - q * x)) & (l_mod - 1);
    }
    return (uint32_t)x;
}

/* -------------------------------------------------------------------------
 * Per-q NTT parameter computation
 * ------------------------------------------------------------------------- */

int chipmunk_ntt_params_compute(chipmunk_ntt_ctx_t *a_ctx, uint64_t q)
{
    if (!a_ctx) return -1;
    if (q == 0 || (q & 1u) == 0) return -1;

    /* q must fit in int32_t for all kernel intermediates (q^2 < 2^62). */
    if (q > 0x7FFFFFFFu) {
        log_it(L_ERROR, "chipmunk_ntt: q=%lu exceeds 31-bit limit", (unsigned long)q);
        return -1;
    }

    memset(a_ctx, 0, sizeof(*a_ctx));

    /* Montgomery parameters: R = 2^32 to match dap_ntt32 SIMD kernel guard.
     * All SIMD backends (AVX2/AVX-512/NEON) require mont_r_bits == 32. */
    uint32_t l_r_bits = 32;
    uint32_t l_r_mask = 0xFFFFFFFFu;
    uint64_t l_R = 1ULL << l_r_bits;

    /* qinv = -q^{-1} mod 2^32 (stored unsigned; dap_ntt uses it as uint32). */
    uint32_t l_qinv_pos = s_modinv_pow2(q, l_r_bits);
    uint32_t l_qinv_neg = (uint32_t)(l_R - l_qinv_pos) & l_r_mask;

    /* R mod q — used to convert standard-form zetas to Montgomery domain. */
    int32_t l_R_mod_q = (int32_t)(l_R % q);

    /* one_over_n = N^{-1} mod q = 512^{-1} mod q (via Fermat). */
    int32_t l_one_over_n = chipmunk_field_inv_q((int32_t)CHIPMUNK_N, q);

    /* Primitive 512th root of unity (2-adicity of q-1 must be >= 9). */
    int32_t l_omega;
    int l_rc = chipmunk_field_primitive_root_2k_q(9, &l_omega, q);
    if (l_rc != 0) {
        log_it(L_ERROR, "chipmunk_ntt: no primitive 512th root for q=%lu",
               (unsigned long)q);
        return l_rc;
    }

    /* Build twiddle tables in MONTGOMERY form.
     *
     * dap_ntt_forward_mont butterfly: s_montgomery_reduce_raw(zeta_mont * coeff)
     * yields zeta_true * coeff. Therefore zeta_mont = zeta_true * R mod q.
     */
    int32_t *l_zetas = (int32_t *)calloc(1024, sizeof(int32_t));
    int32_t *l_zetas_inv = (int32_t *)calloc(1024, sizeof(int32_t));
    if (!l_zetas || !l_zetas_inv) {
        free(l_zetas); free(l_zetas_inv);
        return -1;
    }

    int32_t l_omega_inv = chipmunk_field_pow_q(l_omega, (uint32_t)(CHIPMUNK_N - 1), q);

    /* Forward zetas: omega^{brv9(i)} * R mod q. */
    l_zetas[0] = l_R_mod_q;  /* 1 * R mod q */
    for (int i = 1; i < 512; ++i) {
        uint32_t l_brv = 0;
        uint32_t l_tmp = (uint32_t)i;
        for (int b = 0; b < 9; ++b) {
            l_brv = (l_brv << 1) | (l_tmp & 1u);
            l_tmp >>= 1;
        }
        int32_t l_pow = chipmunk_field_pow_q(l_omega, l_brv, q);
        l_zetas[i] = (int32_t)(((int64_t)l_pow * l_R_mod_q) % (int64_t)q);
    }
    for (int i = 512; i < 1024; ++i) l_zetas[i] = 0;

    /* Inverse zetas: omega^{-brv9(i)} * R mod q. */
    l_zetas_inv[0] = l_R_mod_q;
    for (int i = 1; i < 512; ++i) {
        uint32_t l_brv = 0;
        uint32_t l_tmp = (uint32_t)i;
        for (int b = 0; b < 9; ++b) {
            l_brv = (l_brv << 1) | (l_tmp & 1u);
            l_tmp >>= 1;
        }
        int32_t l_pow = chipmunk_field_pow_q(l_omega_inv, l_brv, q);
        l_zetas_inv[i] = (int32_t)(((int64_t)l_pow * l_R_mod_q) % (int64_t)q);
    }
    for (int i = 512; i < 1024; ++i) l_zetas_inv[i] = 0;

    /* Fill the dap_ntt_params_t. */
    a_ctx->params.n            = CHIPMUNK_N;
    a_ctx->params.q            = (int32_t)q;
    a_ctx->params.qinv         = l_qinv_neg;
    a_ctx->params.mont_r_bits  = l_r_bits;
    a_ctx->params.mont_r_mask  = l_r_mask;
    a_ctx->params.one_over_n   = l_one_over_n;
    a_ctx->params.zetas        = l_zetas;
    a_ctx->params.zetas_inv    = l_zetas_inv;
    a_ctx->params.zetas_len    = 1024;
    a_ctx->q                   = q;
    a_ctx->owns_tables         = true;

    log_it(L_DEBUG, "chipmunk_ntt: per-q params computed: q=%lu R/q=%d 1/N=%d omega=%d",
           (unsigned long)q, l_R_mod_q, l_one_over_n, l_omega);
    return 0;
}

void chipmunk_ntt_ctx_free(chipmunk_ntt_ctx_t *a_ctx)
{
    if (!a_ctx || !a_ctx->owns_tables) return;
    if (a_ctx->params.zetas) {
        dap_memwipe((void*)a_ctx->params.zetas, 1024 * sizeof(int32_t));
        free((void*)a_ctx->params.zetas);
    }
    if (a_ctx->params.zetas_inv) {
        dap_memwipe((void*)a_ctx->params.zetas_inv, 1024 * sizeof(int32_t));
        free((void*)a_ctx->params.zetas_inv);
    }
    memset(a_ctx, 0, sizeof(*a_ctx));
}

/* -------------------------------------------------------------------------
 * Per-q NTT wrappers — Montgomery kernel + post-pass
 * ------------------------------------------------------------------------- */

void chipmunk_ntt_q(int32_t a_r[CHIPMUNK_N], const chipmunk_ntt_ctx_t *a_ctx)
{
    dap_ntt_forward_mont(a_r, &a_ctx->params);
}

void chipmunk_invntt_q(int32_t a_r[CHIPMUNK_N], const chipmunk_ntt_ctx_t *a_ctx)
{
    dap_ntt_inverse_mont(a_r, &a_ctx->params);
    /* Post-pass: cancel Montgomery R and apply 1/N in one step.
     * Mont(c, one_over_n) = c * one_over_n * R^{-1} mod q → standard [0,q). */
    int32_t l_inv_n = a_ctx->params.one_over_n;
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        a_r[i] = dap_ntt_montgomery_reduce((int64_t)a_r[i] * l_inv_n, &a_ctx->params);
    }
    /* Center to [-q/2, q/2) to match the old plain kernel output convention. */
    int32_t l_q = a_ctx->params.q;
    int32_t l_half = l_q / 2;
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        if (a_r[i] > l_half) a_r[i] -= l_q;
    }
}

/* -------------------------------------------------------------------------
 * Montgomery helpers
 * ------------------------------------------------------------------------- */

int32_t chipmunk_ntt_montgomery_multiply_q(int32_t a_a, int32_t a_b, uint64_t q,
                                             uint32_t qinv_neg, uint32_t mont_r_bits,
                                             uint32_t mont_r_mask)
{
    int32_t l_q = (int32_t)q;
    int64_t l_t = (int64_t)a_a * a_b;
    uint32_t l_u = (uint32_t)(l_t & mont_r_mask) * qinv_neg;
    l_u &= mont_r_mask;
    l_t += (int64_t)l_u * l_q;
    int32_t l_result = (int32_t)(l_t >> mont_r_bits);

    /* Branchless final reduction into [0, q). */
    int32_t l_mask_ge = (int32_t)(((uint32_t)l_result - (uint32_t)l_q) >> 31) - 1;
    l_result -= l_mask_ge & l_q;
    int32_t l_mask_lt = l_result >> 31;
    l_result += l_mask_lt & l_q;
    return l_result;
}

int chipmunk_ntt_pointwise_montgomery_q(int32_t a_c[CHIPMUNK_N],
                                          const int32_t a_a[CHIPMUNK_N],
                                          const int32_t a_b[CHIPMUNK_N],
                                          const chipmunk_ntt_ctx_t *a_ctx)
{
    if (!a_c || !a_a || !a_b || !a_ctx) return -1;
    /* Use the dispatched pointwise multiply (SIMD when available). */
    dap_ntt_pointwise_montgomery(a_c, a_a, a_b, &a_ctx->params);
    return 0;
}

/* -------------------------------------------------------------------------
 * Domain conversion helpers (to_mont / from_mont)
 *
 * to_mont(c)   = Mont(c, R^2 mod q)  = c * R mod q  (standard → Montgomery)
 * from_mont(c) = Mont(c, 1)          = c * R^{-1} mod q (Montgomery → standard)
 *
 * These are needed at I/O boundaries: samplers produce standard form,
 * rejection sampling / hash absorb / wire format need standard form.
 * ------------------------------------------------------------------------- */

int32_t chipmunk_ntt_to_mont(int32_t a_c, const chipmunk_ntt_ctx_t *a_ctx)
{
    int32_t l_q = a_ctx->params.q;
    /* R^2 mod q precomputed; Mont(c, R^2) = c * R^2 * R^{-1} = c * R mod q */
    int64_t l_R2_mod_q = ((int64_t)1 << a_ctx->params.mont_r_bits) % l_q;
    l_R2_mod_q = (l_R2_mod_q * l_R2_mod_q) % l_q;
    return dap_ntt_montgomery_reduce((int64_t)a_c * (int32_t)l_R2_mod_q, &a_ctx->params);
}

int32_t chipmunk_ntt_from_mont(int32_t a_c, const chipmunk_ntt_ctx_t *a_ctx)
{
    /* Mont(c, 1) = c * R^{-1} mod q → standard form */
    return dap_ntt_montgomery_reduce((int64_t)a_c, &a_ctx->params);
}

/* -------------------------------------------------------------------------
 * Global-context wrappers (CHIPMUNK_Q)
 * ------------------------------------------------------------------------- */

void chipmunk_ntt(int32_t a_r[CHIPMUNK_N])
{
    if (s_global_ensure() != 0) return;
    chipmunk_ntt_q(a_r, &s_global_ctx);
}

void chipmunk_invntt(int32_t a_r[CHIPMUNK_N])
{
    if (s_global_ensure() != 0) return;
    chipmunk_invntt_q(a_r, &s_global_ctx);
}

int chipmunk_ntt_pointwise_montgomery(int32_t a_c[CHIPMUNK_N],
                                     const int32_t a_a[CHIPMUNK_N],
                                     const int32_t a_b[CHIPMUNK_N])
{
    if (s_global_ensure() != 0) return -1;
    return chipmunk_ntt_pointwise_montgomery_q(a_c, a_a, a_b, &s_global_ctx);
}
