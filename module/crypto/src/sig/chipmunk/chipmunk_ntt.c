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

static void s_global_cleanup(void)
{
    chipmunk_ntt_ctx_free(&s_global_ctx);
}

static void s_global_init(void)
{
    s_global_init_result =
        chipmunk_ntt_params_compute(&s_global_ctx, (uint64_t)CHIPMUNK_Q);
    if (s_global_init_result != 0) {
        log_it(L_CRITICAL, "chipmunk_ntt: global ctx init FAILED for q=%d (rc=%d)",
               CHIPMUNK_Q, s_global_init_result);
        return;
    }
    /* Register cleanup at process exit to avoid leaking twiddle tables. */
    atexit(s_global_cleanup);
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

/* Phase 9.15: Full ctx accessor for ψ-twist in batch verify. */
const chipmunk_ntt_ctx_t *chipmunk_ntt_global_ctx(void)
{
    if (s_global_ensure() != 0) return NULL;
    return &s_global_ctx;
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

    /* Cache negacyclic NTT roots for chipmunk_ntt_q / chipmunk_invntt_q. */
    int32_t l_neg1 = (int32_t)q - 1;
    int32_t l_psi_cached = 0;
    for (uint64_t g = 2; g < 1000; g++) {
        int32_t candidate = chipmunk_field_pow_q((int32_t)g,
            (uint32_t)((q - 1) / (2 * CHIPMUNK_N)), q);
        if (chipmunk_field_pow_q(candidate, CHIPMUNK_N, q) == l_neg1) {
            l_psi_cached = candidate;
            break;
        }
    }
    if (l_psi_cached == 0) {
        log_it(L_ERROR, "chipmunk_ntt: no primitive 2N-th root for q=%lu",
               (unsigned long)q);
        free(l_zetas); free(l_zetas_inv);
        return -1;
    }
    a_ctx->psi       = l_psi_cached;
    a_ctx->psi_inv   = chipmunk_field_inv_q(l_psi_cached, q);
    a_ctx->omega     = l_omega;
    a_ctx->omega_inv = l_omega_inv;

    /* Pre-compute twiddle tables for CT-DIT / GS-DIF (standard form).
     * Avoids chipmunk_field_pow_q per stage — just table lookups. */
    int32_t *l_ct = (int32_t *)calloc(512, sizeof(int32_t));
    int32_t *l_gs = (int32_t *)calloc(512, sizeof(int32_t));
    /* Montgomery-form copies for SIMD butterfly (avoids Barrett in inner loop). */
    int32_t *l_ct_mont = (int32_t *)calloc(512, sizeof(int32_t));
    int32_t *l_gs_mont = (int32_t *)calloc(512, sizeof(int32_t));
    if (!l_ct || !l_gs || !l_ct_mont || !l_gs_mont) {
        free(l_ct); free(l_gs); free(l_ct_mont); free(l_gs_mont); return -1;
    }
    /* CT twiddles: len = 2, 4, ..., N (small to large, matching CT loop) */
    {
        int l_off = 0;
        for (int l_len = 2; l_len <= (int)CHIPMUNK_N; l_len <<= 1) {
            int l_half = l_len / 2;
            int32_t l_w = chipmunk_field_pow_q(l_omega, (uint32_t)(CHIPMUNK_N / l_len), q);
            int32_t l_wn = 1;
            for (int j = 0; j < l_half; ++j) {
                l_ct[l_off + j] = l_wn;
                l_wn = (int32_t)(((int64_t)l_wn * l_w) % (int64_t)q);
            }
            l_off += l_half;
        }
    }
    /* GS twiddles: len = N, N/2, ..., 2 (large to small, matching GS loop) */
    {
        int l_off = 0;
        for (int l_len = (int)CHIPMUNK_N; l_len >= 2; l_len >>= 1) {
            int l_half = l_len / 2;
            int32_t l_w_inv = chipmunk_field_pow_q(l_omega_inv, (uint32_t)(CHIPMUNK_N / l_len), q);
            int32_t l_wn_inv = 1;
            for (int j = 0; j < l_half; ++j) {
                l_gs[l_off + j] = l_wn_inv;
                l_wn_inv = (int32_t)(((int64_t)l_wn_inv * l_w_inv) % (int64_t)q);
            }
            l_off += l_half;
        }
    }
    /* Generate Montgomery-form copies of twiddle tables for SIMD butterfly. */
    for (int i = 0; i < 512; ++i) {
        l_ct_mont[i] = (int32_t)(((int64_t)l_ct[i] * l_R_mod_q) % (int64_t)q);
        l_gs_mont[i] = (int32_t)(((int64_t)l_gs[i] * l_R_mod_q) % (int64_t)q);
    }
    a_ctx->ct_twiddles = l_ct;
    a_ctx->gs_twiddles = l_gs;
    a_ctx->ct_twiddles_mont = l_ct_mont;
    a_ctx->gs_twiddles_mont = l_gs_mont;
    a_ctx->inv_q = 1.0 / (double)q;

    log_it(L_DEBUG, "chipmunk_ntt: per-q params computed: q=%lu R/q=%d 1/N=%d psi=%d omega=%d",
           (unsigned long)q, l_R_mod_q, l_one_over_n, l_psi_cached, l_omega);

    return 0;
}

void chipmunk_ntt_ctx_free(chipmunk_ntt_ctx_t *a_ctx)
{
    if (!a_ctx || !a_ctx->owns_tables) return;
    free(a_ctx->ct_twiddles);
    free(a_ctx->gs_twiddles);
    free(a_ctx->ct_twiddles_mont);
    free(a_ctx->gs_twiddles_mont);
    a_ctx->ct_twiddles = NULL;
    a_ctx->gs_twiddles = NULL;
    a_ctx->ct_twiddles_mont = NULL;
    a_ctx->gs_twiddles_mont = NULL;
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

/* Barrett fast-mod: x mod q using pre-computed 1.0/q.
 * Replaces expensive 64-bit % q with a floating-point multiply.
 * Correct for |x| < q^2 ≈ 10^13 (fits in double mantissa). */
static inline int32_t s_barrett_mod(int64_t x, int32_t q, double inv_q)
{
    int64_t r = x - (int64_t)((double)x * inv_q) * q;
    if (r >= q)  r -= q;
    if (r < 0)   r += q;
    return (int32_t)r;
}

/* Fast cyclic NTT using iterative Cooley-Tukey (in-place, natural order).
 * Uses pre-computed twiddle tables and Barrett reduction. */
static void s_cyclic_ct_ntt(int32_t a_r[CHIPMUNK_N], const chipmunk_ntt_ctx_t *a_ctx)
{
    int32_t l_q = (int32_t)a_ctx->q;
    double l_inv_q = a_ctx->inv_q;
    /* Bit-reverse the input */
    for (int i = 1, j = 0; i < CHIPMUNK_N; i++) {
        int bit = CHIPMUNK_N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            int32_t t = a_r[i]; a_r[i] = a_r[j]; a_r[j] = t;
        }
    }
    /* CT butterflies with pre-computed twiddles and Barrett reduction */
    int l_off = 0;
    for (int len = 2; len <= CHIPMUNK_N; len <<= 1) {
        int l_half = len / 2;
        for (int i = 0; i < CHIPMUNK_N; i += len) {
            for (int j = 0; j < l_half; j++) {
                int32_t u = a_r[i + j];
                int32_t wn = a_ctx->ct_twiddles[l_off + j];
                int32_t v = s_barrett_mod((int64_t)a_r[i + j + l_half] * wn, l_q, l_inv_q);
                a_r[i + j]            = s_barrett_mod((int64_t)u + v, l_q, l_inv_q);
                a_r[i + j + l_half]   = s_barrett_mod((int64_t)u - v, l_q, l_inv_q);
            }
        }
        l_off += l_half;
    }
}

/* Fast cyclic inverse NTT (GS butterflies, natural order output). */
static void s_cyclic_gs_invntt(int32_t a_r[CHIPMUNK_N], const chipmunk_ntt_ctx_t *a_ctx)
{
    int32_t l_q = (int32_t)a_ctx->q;
    double l_inv_q = a_ctx->inv_q;
    int l_off = 0;
    for (int len = CHIPMUNK_N; len >= 2; len >>= 1) {
        int l_half = len / 2;
        for (int i = 0; i < CHIPMUNK_N; i += len) {
            for (int j = 0; j < l_half; j++) {
                int32_t u = a_r[i + j];
                int32_t v = a_r[i + j + l_half];
                a_r[i + j]          = s_barrett_mod((int64_t)u + v, l_q, l_inv_q);
                int32_t wn = a_ctx->gs_twiddles[l_off + j];
                a_r[i + j + l_half] = s_barrett_mod((int64_t)(u - v) * wn, l_q, l_inv_q);
            }
        }
        l_off += l_half;
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
    /* Scale by 1/N with Barrett reduction */
    int32_t l_n_inv = chipmunk_field_inv_q(CHIPMUNK_N, a_ctx->q);
    for (int i = 0; i < CHIPMUNK_N; i++) {
        a_r[i] = s_barrett_mod((int64_t)a_r[i] * l_n_inv, l_q, l_inv_q);
    }
}

/* -------------------------------------------------------------------------
 * AVX2-accelerated cyclic NTT (CT-DIT with Montgomery-form twiddles)
 *
 * Uses Montgomery multiply-reduce in the butterfly instead of Barrett.
 * The twiddle table must be in Montgomery form (ct_twiddles_mont).
 * Input/output: standard form. The Montgomery R from twiddles cancels
 * with the Montgomery reduce in the butterfly, preserving standard form.
 * ------------------------------------------------------------------------- */
#if defined(__x86_64__) && defined(__AVX2__)
#include <immintrin.h>

/* Montgomery reduce: (a * b) * R^{-1} mod q for 8 int32 pairs.
 * Uses even/odd lane separation with _mm256_mul_epi32 (signed widening). */
static inline __m256i s_mont_reduce_mul_avx2(__m256i a, __m256i b,
                                              __m256i qinv_vec, __m256i q_vec)
{
    /* Even lanes: a[0]*b[0], a[2]*b[2], a[4]*b[4], a[6]*b[6] → 4 int64 */
    __m256i ae = _mm256_mul_epi32(a, b);
    /* Odd lanes: shift right 32 to align, then multiply */
    __m256i a_odd = _mm256_srli_epi64(a, 32);
    __m256i b_odd = _mm256_srli_epi64(b, 32);
    __m256i ao = _mm256_mul_epi32(a_odd, b_odd);

    /* Montgomery reduce even lanes */
    __m256i mask32 = _mm256_set1_epi64x(0xFFFFFFFF);
    __m256i ae_lo = _mm256_and_si256(ae, mask32);
    __m256i ue = _mm256_mul_epu32(ae_lo, qinv_vec);
    ue = _mm256_and_si256(ue, mask32);
    __m256i uqe = _mm256_mul_epi32(ue, q_vec);
    __m256i te = _mm256_add_epi64(ae, uqe);
    __m256i re = _mm256_srli_epi64(te, 32);

    /* Montgomery reduce odd lanes */
    __m256i ao_lo = _mm256_and_si256(ao, mask32);
    __m256i uo = _mm256_mul_epu32(ao_lo, qinv_vec);
    uo = _mm256_and_si256(uo, mask32);
    __m256i uqo = _mm256_mul_epi32(uo, q_vec);
    __m256i to = _mm256_add_epi64(ao, uqo);
    __m256i ro = _mm256_srli_epi64(to, 32);

    /* Interleave even/odd results back to 8 int32 */
    __m256i re_lo = _mm256_and_si256(re, mask32); /* even results in low32 */
    __m256i ro_sh = _mm256_slli_epi64(ro, 32);    /* odd results in high32 */
    return _mm256_or_si256(re_lo, ro_sh);
}

__attribute__((target("avx2")))
static void s_cyclic_ct_ntt_avx2(int32_t a_r[CHIPMUNK_N], const chipmunk_ntt_ctx_t *a_ctx)
{
    int32_t l_q = (int32_t)a_ctx->q;
    __m256i l_qinv_vec = _mm256_set1_epi32((int32_t)a_ctx->params.qinv);
    __m256i l_q_vec = _mm256_set1_epi32(l_q);

    /* Bit-reverse the input */
    for (int i = 1, j = 0; i < CHIPMUNK_N; i++) {
        int bit = CHIPMUNK_N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            int32_t t = a_r[i]; a_r[i] = a_r[j]; a_r[j] = t;
        }
    }

    /* CT butterflies with Montgomery-form twiddles */
    int l_off = 0;
    for (int len = 2; len <= CHIPMUNK_N; len <<= 1) {
        int l_half = len / 2;
        if (l_half >= 8) {
            /* SIMD path: process 8 butterflies at a time */
            for (int i = 0; i < CHIPMUNK_N; i += len) {
                for (int j = 0; j < l_half; j += 8) {
                    __m256i u = _mm256_loadu_si256((__m256i*)(a_r + i + j));
                    __m256i v = _mm256_loadu_si256((__m256i*)(a_r + i + j + l_half));
                    __m256i wn = _mm256_loadu_si256((__m256i*)(a_ctx->ct_twiddles_mont + l_off + j));
                    __m256i t = s_mont_reduce_mul_avx2(v, wn, l_qinv_vec, l_q_vec);
                    _mm256_storeu_si256((__m256i*)(a_r + i + j),
                        _mm256_add_epi32(u, t));
                    _mm256_storeu_si256((__m256i*)(a_r + i + j + l_half),
                        _mm256_sub_epi32(u, t));
                }
            }
        } else {
            /* Scalar fallback for small half sizes */
            for (int i = 0; i < CHIPMUNK_N; i += len) {
                for (int j = 0; j < l_half; j++) {
                    int32_t u = a_r[i + j];
                    int32_t v = s_barrett_mod((int64_t)a_r[i + j + l_half] *
                                              a_ctx->ct_twiddles[l_off + j], l_q, a_ctx->inv_q);
                    a_r[i + j]          = s_barrett_mod((int64_t)u + v, l_q, a_ctx->inv_q);
                    a_r[i + j + l_half] = s_barrett_mod((int64_t)u - v, l_q, a_ctx->inv_q);
                }
            }
        }
        l_off += l_half;
    }
}

__attribute__((target("avx2")))
static void s_cyclic_gs_invntt_avx2(int32_t a_r[CHIPMUNK_N], const chipmunk_ntt_ctx_t *a_ctx)
{
    int32_t l_q = (int32_t)a_ctx->q;
    __m256i l_qinv_vec = _mm256_set1_epi32((int32_t)a_ctx->params.qinv);
    __m256i l_q_vec = _mm256_set1_epi32(l_q);

    int l_off = 0;
    for (int len = CHIPMUNK_N; len >= 2; len >>= 1) {
        int l_half = len / 2;
        if (l_half >= 8) {
            for (int i = 0; i < CHIPMUNK_N; i += len) {
                for (int j = 0; j < l_half; j += 8) {
                    __m256i u = _mm256_loadu_si256((__m256i*)(a_r + i + j));
                    __m256i v = _mm256_loadu_si256((__m256i*)(a_r + i + j + l_half));
                    __m256i wn = _mm256_loadu_si256((__m256i*)(a_ctx->gs_twiddles_mont + l_off + j));
                    __m256i diff = _mm256_sub_epi32(u, v);
                    _mm256_storeu_si256((__m256i*)(a_r + i + j),
                        _mm256_add_epi32(u, v));
                    _mm256_storeu_si256((__m256i*)(a_r + i + j + l_half),
                        s_mont_reduce_mul_avx2(diff, wn, l_qinv_vec, l_q_vec));
                }
            }
        } else {
            for (int i = 0; i < CHIPMUNK_N; i += len) {
                for (int j = 0; j < l_half; j++) {
                    int32_t u = a_r[i + j];
                    int32_t v = a_r[i + j + l_half];
                    a_r[i + j]          = s_barrett_mod((int64_t)u + v, l_q, a_ctx->inv_q);
                    a_r[i + j + l_half] = s_barrett_mod((int64_t)(u - v) *
                                                         a_ctx->gs_twiddles[l_off + j], l_q, a_ctx->inv_q);
                }
            }
        }
        l_off += l_half;
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
    int32_t l_n_inv = chipmunk_field_inv_q(CHIPMUNK_N, a_ctx->q);
    for (int i = 0; i < CHIPMUNK_N; i++) {
        a_r[i] = s_barrett_mod((int64_t)a_r[i] * l_n_inv, l_q, a_ctx->inv_q);
    }
}
#endif /* __AVX2__ */

/* Find primitive 2N-th root of unity psi (psi^N = -1 mod q). */
void chipmunk_ntt_q(int32_t a_r[CHIPMUNK_N], const chipmunk_ntt_ctx_t *a_ctx)
{
    if (!a_ctx || a_ctx->psi == 0) return;  /* invalid ctx — no-op */
    int32_t l_q = (int32_t)a_ctx->q;
    double l_inv_q = a_ctx->inv_q;
    int32_t l_psi = a_ctx->psi;

    /* Pre-multiply by psi^j to convert negacyclic → cyclic */
    int32_t l_psi_j = 1;
    for (int j = 0; j < CHIPMUNK_N; j++) {
        a_r[j] = s_barrett_mod((int64_t)a_r[j] * l_psi_j, l_q, l_inv_q);
        l_psi_j = s_barrett_mod((int64_t)l_psi_j * l_psi, l_q, l_inv_q);
    }

    /* omega = psi^2 (primitive N-th root) — use optimized CT-DIT */
#if defined(__x86_64__) && defined(__AVX2__)
    if (a_ctx->ct_twiddles_mont && a_ctx->params.qinv) {
        s_cyclic_ct_ntt_avx2(a_r, a_ctx);
    } else
#endif
    {
        s_cyclic_ct_ntt(a_r, a_ctx);
    }
}

void chipmunk_invntt_q(int32_t a_r[CHIPMUNK_N], const chipmunk_ntt_ctx_t *a_ctx)
{
    if (!a_ctx || a_ctx->psi == 0) return;  /* invalid ctx — no-op */
    int32_t l_q = (int32_t)a_ctx->q;
    double l_inv_q = a_ctx->inv_q;
    int32_t l_psi_inv = a_ctx->psi_inv;

    /* Inverse cyclic NTT — use optimized GS-DIF */
#if defined(__x86_64__) && defined(__AVX2__)
    if (a_ctx->gs_twiddles_mont && a_ctx->params.qinv) {
        s_cyclic_gs_invntt_avx2(a_r, a_ctx);
    } else
#endif
    {
        s_cyclic_gs_invntt(a_r, a_ctx);
    }

    /* Post-multiply by psi^{-j} to convert cyclic → negacyclic */
    int32_t l_psi_inv_j = 1;
    for (int j = 0; j < CHIPMUNK_N; j++) {
        a_r[j] = s_barrett_mod((int64_t)a_r[j] * l_psi_inv_j, l_q, l_inv_q);
        l_psi_inv_j = s_barrett_mod((int64_t)l_psi_inv_j * l_psi_inv, l_q, l_inv_q);
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
