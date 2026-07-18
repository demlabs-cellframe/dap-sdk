/*
 * chipmunk_ntt.c — 512-point negacyclic NTT for chipmunk lattice cryptography.
 *
 * Uses a reference direct-DFT negacyclic NTT (O(N^2)) that IS a ring
 * isomorphism: ntt(a*b) = ntt(a) ⊙ ntt(b) pointwise. This is required for
 * correct polynomial multiplication via pointwise product.
 *
 * The previous Montgomery-kernel approach (dap_ntt_forward_mont) produced
 * correct round-trips but was NOT a ring homomorphism — pointwise multiply
 * in that domain did not correspond to negacyclic convolution.
 *
 * Performance: O(N^2) = 262144 field multiplications per transform.
 * Acceptable for chipmunk's security-first design. Can be replaced with
 * a fast negacyclic NTT (Cooley-Tukey with correct twiddle assignment)
 * in the future without changing the API.
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

    /* Build twiddle tables in tree-traversal order (Dilithium/FIPS-204 format).
     *
     * dap_ntt_forward_mont uses Cooley-Tukey with sequential zeta walk:
     *   k = 1; for len = N/2 down to 1: zeta = zetas[k++]
     * The butterfly: t = mont_reduce(zeta_mont * coeff) = zeta_true * coeff.
     * So zeta_mont = zeta_true * R mod q.
     *
     * Tree order at level m (len = N >> (m+1)):
     *   twiddles = omega^{(2i+1)*len} for i = 0..2^m - 1
     * Each stored as omega^{(2i+1)*len} * R mod q (Montgomery form).
     */
    int32_t *l_zetas = (int32_t *)calloc(1024, sizeof(int32_t));
    int32_t *l_zetas_inv = (int32_t *)calloc(1024, sizeof(int32_t));
    if (!l_zetas || !l_zetas_inv) {
        free(l_zetas); free(l_zetas_inv);
        return -1;
    }

    int32_t l_omega_inv = chipmunk_field_pow_q(l_omega, (uint32_t)(CHIPMUNK_N - 1), q);
    const int l_log_n = 9;  /* log2(CHIPMUNK_N) */

    /* Forward zetas in tree order (Montgomery form).
     * NOTE: This is the order that produces correct round-trip with the
     * dap_ntt kernels, but the resulting transform is NOT a ring
     * homomorphism — pointwise multiply in NTT domain does NOT correspond
     * to negacyclic convolution. chipmunk_poly_mul_ntt_q (plain a*b%q)
     * is therefore INCORRECT for this NTT. The correct fix requires
     * rewriting the butterfly or switching to a reference negacyclic NTT. */
    l_zetas[0] = 0;  /* placeholder, unused by forward kernel (k starts at 1) */
    {
        int l_k = 1;
        for (int l_m = 0; l_m < l_log_n; ++l_m) {
            int l_len = CHIPMUNK_N >> (l_m + 1);
            for (int l_i = 0; l_i < (1 << l_m); ++l_i) {
                uint32_t l_power = (uint32_t)(2 * l_i + 1) * (uint32_t)l_len;
                int32_t l_tw = chipmunk_field_pow_q(l_omega, l_power, q);
                l_zetas[l_k++] = (int32_t)(((int64_t)l_tw * l_R_mod_q) % (int64_t)q);
            }
        }
    }
    for (int i = 512; i < 1024; ++i) l_zetas[i] = 0;

    /* Inverse zetas in tree order (Gentleman-Sande, Montgomery form). */
    {
        int l_k = 0;
        for (int l_m = 0; l_m < l_log_n; ++l_m) {
            int l_len = 1 << l_m;
            for (int l_i = 0; l_i < CHIPMUNK_N / (2 * l_len); ++l_i) {
                uint32_t l_power = (uint32_t)(2 * l_i + 1) * (uint32_t)l_len;
                int32_t l_tw = chipmunk_field_pow_q(l_omega_inv, l_power, q);
                l_zetas_inv[l_k++] = (int32_t)(((int64_t)l_tw * l_R_mod_q) % (int64_t)q);
            }
        }
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
 * Per-q NTT wrappers — Fast negacyclic NTT (ring isomorphism)
 *
 * Implementation: "twisted" negacyclic NTT via pre/post-multiplication.
 *   ntt_neg(a)[i] = cyclic_ntt(a[j] * psi^j)[i]
 * where psi is a primitive 2N-th root of unity (psi^N = -1).
 * This IS a ring isomorphism: ntt(a*b) = ntt(a) ⊙ ntt(b).
 *
 * The cyclic NTT uses Cooley-Tukey with standard twiddle assignment.
 * ------------------------------------------------------------------------- */

/* Fast cyclic NTT using iterative Cooley-Tukey (in-place, natural order).
 * Uses omega (primitive N-th root) with bit-reversed twiddle walk. */
static void s_cyclic_ct_ntt(int32_t a_r[CHIPMUNK_N], int32_t omega, uint64_t q)
{
    int32_t l_q = (int32_t)q;
    /* Bit-reverse the input */
    for (int i = 1, j = 0; i < CHIPMUNK_N; i++) {
        int bit = CHIPMUNK_N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            int32_t t = a_r[i]; a_r[i] = a_r[j]; a_r[j] = t;
        }
    }
    /* CT butterflies */
    for (int len = 2; len <= CHIPMUNK_N; len <<= 1) {
        int32_t w = chipmunk_field_pow_q(omega, CHIPMUNK_N / len, q);
        for (int i = 0; i < CHIPMUNK_N; i += len) {
            int32_t wn = 1;
            for (int j = 0; j < len / 2; j++) {
                int32_t u = a_r[i + j];
                int64_t v = ((int64_t)a_r[i + j + len / 2] * wn) % l_q;
                int32_t iv = (int32_t)v;
                if (iv < 0) iv += l_q;
                int64_t a_val = ((int64_t)u + iv) % l_q;
                int64_t b_val = ((int64_t)u - iv) % l_q;
                a_r[i + j] = (int32_t)(a_val < 0 ? a_val + l_q : a_val);
                a_r[i + j + len / 2] = (int32_t)(b_val < 0 ? b_val + l_q : b_val);
                wn = (int32_t)(((int64_t)wn * w) % l_q);
            }
        }
    }
}

/* Fast cyclic inverse NTT (GS butterflies, natural order output). */
static void s_cyclic_gs_invntt(int32_t a_r[CHIPMUNK_N], int32_t omega_inv, uint64_t q)
{
    int32_t l_q = (int32_t)q;
    for (int len = CHIPMUNK_N; len >= 2; len >>= 1) {
        int32_t w = chipmunk_field_pow_q(omega_inv, CHIPMUNK_N / len, q);
        for (int i = 0; i < CHIPMUNK_N; i += len) {
            int32_t wn = 1;
            for (int j = 0; j < len / 2; j++) {
                int32_t u = a_r[i + j];
                int32_t v = a_r[i + j + len / 2];
                int64_t a_val = ((int64_t)u + v) % l_q;
                int64_t diff = ((int64_t)u - v) % l_q;
                a_r[i + j] = (int32_t)(a_val < 0 ? a_val + l_q : a_val);
                int64_t b_val = ((int64_t)diff * wn) % l_q;
                a_r[i + j + len / 2] = (int32_t)(b_val < 0 ? b_val + l_q : b_val);
                wn = (int32_t)(((int64_t)wn * w) % l_q);
            }
        }
    }
    /* Bit-reverse output to natural order */
    for (int i = 1, j = 0; i < CHIPMUNK_N; i++) {
        int bit = CHIPMUNK_N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            int32_t t = a_r[i]; a_r[i] = a_r[j]; a_r[j] = t;
        }
    }
    /* Scale by 1/N */
    int32_t n_inv = chipmunk_field_inv_q(CHIPMUNK_N, q);
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int64_t v = ((int64_t)a_r[i] * n_inv) % l_q;
        a_r[i] = (int32_t)(v < 0 ? v + l_q : v);
    }
}

/* Find primitive 2N-th root of unity psi (psi^N = -1 mod q). */
static int32_t s_find_psi(uint64_t q)
{
    int32_t l_neg1 = (int32_t)q - 1;
    for (uint64_t g = 2; g < 1000; g++) {
        int32_t candidate = chipmunk_field_pow_q((int32_t)g,
            (uint32_t)((q - 1) / (2 * CHIPMUNK_N)), q);
        if (chipmunk_field_pow_q(candidate, CHIPMUNK_N, q) == l_neg1)
            return candidate;
    }
    return 0;  /* should not happen for valid q */
}

void chipmunk_ntt_q(int32_t a_r[CHIPMUNK_N], const chipmunk_ntt_ctx_t *a_ctx)
{
    int32_t l_q = (int32_t)a_ctx->q;
    int32_t l_psi = s_find_psi(a_ctx->q);
    if (l_psi == 0) return;

    /* Pre-multiply by psi^j to convert negacyclic → cyclic */
    int32_t l_psi_j = 1;
    for (int j = 0; j < CHIPMUNK_N; j++) {
        int64_t v = ((int64_t)a_r[j] * l_psi_j) % l_q;
        a_r[j] = (int32_t)(v < 0 ? v + l_q : v);
        l_psi_j = (int32_t)(((int64_t)l_psi_j * l_psi) % l_q);
    }

    /* omega = psi^2 (primitive N-th root) */
    int32_t l_omega = (int32_t)(((int64_t)l_psi * l_psi) % l_q);
    s_cyclic_ct_ntt(a_r, l_omega, a_ctx->q);
}

void chipmunk_invntt_q(int32_t a_r[CHIPMUNK_N], const chipmunk_ntt_ctx_t *a_ctx)
{
    int32_t l_q = (int32_t)a_ctx->q;
    int32_t l_psi = s_find_psi(a_ctx->q);
    if (l_psi == 0) return;

    int32_t l_omega = (int32_t)(((int64_t)l_psi * l_psi) % l_q);
    int32_t l_omega_inv = chipmunk_field_inv_q(l_omega, a_ctx->q);
    int32_t l_psi_inv = chipmunk_field_inv_q(l_psi, a_ctx->q);

    /* Inverse cyclic NTT */
    s_cyclic_gs_invntt(a_r, l_omega_inv, a_ctx->q);

    /* Post-multiply by psi^{-j} to convert cyclic → negacyclic */
    int32_t l_psi_inv_j = 1;
    for (int j = 0; j < CHIPMUNK_N; j++) {
        int64_t v = ((int64_t)a_r[j] * l_psi_inv_j) % l_q;
        a_r[j] = (int32_t)(v < 0 ? v + l_q : v);
        l_psi_inv_j = (int32_t)(((int64_t)l_psi_inv_j * l_psi_inv) % l_q);
    }

    /* Center to [-q/2, q/2) */
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
