/*
 * chipmunk_fri_ntt.c — 2048-point NTT for FRI-DEEP polynomial commitment.
 *
 * Standard cyclic NTT over F_q (q = 3168257), non-Montgomery.
 *
 * Uses decimation-in-frequency (DIF) forward / decimation-in-time (DIT)
 * inverse pair with pre/post bit-reversal:
 *
 *   forward:  pre-BRV data → DIF stages → natural-order evaluations
 *             stages s=0..logN-1, twiddle stride = 2^{logN-1-s}
 *
 *   inverse:  natural-order evals → DIT stages (reverse) → post-BRV → 1/N
 *             stages s=logN-1..0, twiddle stride = 2^{logN-1-s}
 *
 * Twiddles: zetas[k] = omega^k (standard power order).
 * Each butterfly selects zetas[j * stride] where stride halves
 * each forward stage (or doubles each inverse stage).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "chipmunk_fri_ntt.h"
#include "chipmunk.h"
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>

#include "dap_common.h"
#include "dap_memwipe.h"

#define LOG_TAG "chipmunk_fri_ntt"

/* -------------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------------- */

static int32_t s_fri_zetas[CHIPMUNK_FRI_NTT_SIZE];      /* omega^k */
static int32_t s_fri_zetas_inv[CHIPMUNK_FRI_NTT_SIZE];  /* omega_inv^k */
static atomic_int s_fri_ntt_state = 0;
static pthread_mutex_t s_fri_ntt_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Field multiplication in [0, q).  Hot path — kept inline. */
static inline int32_t s_fri_fqmul(int32_t a_a, int32_t a_b)
{
    int64_t l_t = (int64_t)a_a * (int64_t)a_b;
    int32_t l_r = (int32_t)(l_t % (int64_t)CHIPMUNK_Q);
    if (l_r < 0) l_r += (int32_t)CHIPMUNK_Q;
    return l_r;
}

/* Parameterized field multiplication (Phase 9.13h). */
static inline int32_t s_fri_fqmul_q(int32_t a_a, int32_t a_b, uint64_t q)
{
    int64_t l_t = (int64_t)a_a * (int64_t)a_b;
    int32_t l_r = (int32_t)(l_t % (int64_t)q);
    if (l_r < 0) l_r += (int32_t)q;
    return l_r;
}

/* Generate standard twiddle tables: zetas[k] = omega^k. */
static int s_fri_ntt_init_locked(void)
{
    int l_rc = chipmunk_field_init();
    if (l_rc != 0) {
        log_it(L_CRITICAL, "FRI NTT: chipmunk_field_init failed");
        return l_rc;
    }

    int32_t l_omega = chipmunk_field_omega_2048();
    int32_t l_omega_inv = chipmunk_field_omega_2048_inv();

    /* Build twiddle tables */
    s_fri_zetas[0] = 1;
    s_fri_zetas_inv[0] = 1;
    for (unsigned int k = 1; k < CHIPMUNK_FRI_NTT_SIZE; ++k) {
        s_fri_zetas[k]     = s_fri_fqmul(s_fri_zetas[k - 1], l_omega);
        s_fri_zetas_inv[k] = s_fri_fqmul(s_fri_zetas_inv[k - 1], l_omega_inv);
    }

    /* Verification */
    int32_t l_check = s_fri_fqmul(l_omega, l_omega_inv);
    if (l_check != 1) {
        log_it(L_CRITICAL, "FRI NTT: omega * omega_inv = %d (expected 1)", l_check);
        return -1;
    }

    log_it(L_DEBUG, "FRI NTT initialised: omega=%d, N=%d",
           l_omega, CHIPMUNK_FRI_NTT_SIZE);
    return 0;
}

int chipmunk_fri_ntt_init(void)
{
    int l_state = atomic_load_explicit(&s_fri_ntt_state, memory_order_acquire);
    if (l_state == 2) {
        return 0;
    }

    pthread_mutex_lock(&s_fri_ntt_mutex);
    l_state = atomic_load_explicit(&s_fri_ntt_state, memory_order_acquire);
    if (l_state == 2) {
        pthread_mutex_unlock(&s_fri_ntt_mutex);
        return 0;
    }

    int l_rc = s_fri_ntt_init_locked();
    if (l_rc == 0) {
        atomic_store_explicit(&s_fri_ntt_state, 2, memory_order_release);
    }
    pthread_mutex_unlock(&s_fri_ntt_mutex);
    return l_rc;
}

bool chipmunk_fri_ntt_is_initialized(void)
{
    return atomic_load_explicit(&s_fri_ntt_state, memory_order_acquire) == 2;
}

/* -------------------------------------------------------------------------
 * Per-q NTT context (Phase 9.13h)
 *
 * Holds twiddle tables for an arbitrary prime q. Built by
 * chipmunk_fri_ntt_ctx_init, freed by chipmunk_fri_ntt_ctx_free.
 * The _q forward/inverse/coset_forward variants take this context.
 * ---------------------------------------------------------------------- */

int chipmunk_fri_ntt_ctx_init(chipmunk_fri_ntt_ctx_t *ctx, uint64_t q,
                                uint32_t two_adicity)
{
    if (!ctx) return -1;
    if (q == 0 || (q & 1u) == 0) return -1;
    if (two_adicity != CHIPMUNK_FRI_NTT_LOG) {
        log_it(L_ERROR, "FRI NTT ctx: two_adicity=%u != NTT_LOG=%u (only 2048-pt supported)",
               two_adicity, CHIPMUNK_FRI_NTT_LOG);
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->q = q;

    /* Compute omega and omega_inv for this q via per-q field API. */
    chipmunk_field_consts_t l_fc;
    int l_rc = chipmunk_field_compute_for_q(&l_fc, q, two_adicity);
    if (l_rc != 0) {
        log_it(L_ERROR, "FRI NTT ctx: field constants failed for q=%lu", (unsigned long)q);
        return l_rc;
    }
    ctx->omega = l_fc.omega;
    ctx->omega_inv = l_fc.omega_inv;
    ctx->inv_n = l_fc.inv_domain;

    /* Allocate twiddle tables. */
    ctx->zetas = calloc(CHIPMUNK_FRI_NTT_SIZE, sizeof(int32_t));
    ctx->zetas_inv = calloc(CHIPMUNK_FRI_NTT_SIZE, sizeof(int32_t));
    if (!ctx->zetas || !ctx->zetas_inv) {
        free(ctx->zetas); ctx->zetas = NULL;
        free(ctx->zetas_inv); ctx->zetas_inv = NULL;
        return -1;
    }

    ctx->zetas[0] = 1;
    ctx->zetas_inv[0] = 1;
    for (unsigned k = 1; k < CHIPMUNK_FRI_NTT_SIZE; ++k) {
        ctx->zetas[k] = s_fri_fqmul_q(ctx->zetas[k - 1], ctx->omega, q);
        ctx->zetas_inv[k] = s_fri_fqmul_q(ctx->zetas_inv[k - 1], ctx->omega_inv, q);
    }

    /* Verify omega * omega_inv == 1. */
    if (s_fri_fqmul_q(ctx->omega, ctx->omega_inv, q) != 1) {
        log_it(L_CRITICAL, "FRI NTT ctx: omega*omega_inv != 1 for q=%lu", (unsigned long)q);
        chipmunk_fri_ntt_ctx_free(ctx);
        return -1;
    }

    log_it(L_DEBUG, "FRI NTT ctx init: q=%lu omega=%d", (unsigned long)q, ctx->omega);
    return 0;
}

void chipmunk_fri_ntt_ctx_free(chipmunk_fri_ntt_ctx_t *ctx)
{
    if (!ctx) return;
    if (ctx->zetas) {
        dap_memwipe(ctx->zetas, CHIPMUNK_FRI_NTT_SIZE * sizeof(int32_t));
        free(ctx->zetas);
        ctx->zetas = NULL;
    }
    if (ctx->zetas_inv) {
        dap_memwipe(ctx->zetas_inv, CHIPMUNK_FRI_NTT_SIZE * sizeof(int32_t));
        free(ctx->zetas_inv);
        ctx->zetas_inv = NULL;
    }
    ctx->q = 0;
}

/* -------------------------------------------------------------------------
 * Bit-reverse permutation: in-place.
 * After this, a[i] = a_old[brv11(i)].
 * ------------------------------------------------------------------------- */

static void s_fri_bit_reverse(int32_t a[CHIPMUNK_FRI_NTT_SIZE])
{
    for (unsigned int i = 0; i < CHIPMUNK_FRI_NTT_SIZE; ++i) {
        unsigned int l_j = chipmunk_fri_ntt_brv11(i);
        if (l_j > i) {
            int32_t l_tmp = a[i];
            a[i] = a[l_j];
            a[l_j] = l_tmp;
        }
    }
}

/* -------------------------------------------------------------------------
 * Forward NTT: Decimation-In-Frequency (DIF), in-place.
 *
 * Input:  a[i] in [0, q), standard coefficient order.
 * Output: a[k] = f(omega^k) for k = 0..N-1 (natural order).
 *
 * Algorithm:
 *   1. Bit-reverse input data
 *   2. Stages s = 0..logN-1:
 *      - Butterfly size = 2^{s+1}
 *      - Half-size    = 2^s
 *      - Twiddle stride = 2^{logN-1-s}  (1024, 512, 256, ..., 1)
 *      - For each butterfly j in [0, half-1]:
 *          twiddle = omega^{j * stride}
 * ------------------------------------------------------------------------- */

void chipmunk_fri_ntt_forward(int32_t a[CHIPMUNK_FRI_NTT_SIZE])
{
    /* Step 1: bit-reverse input */
    s_fri_bit_reverse(a);

    /* Step 2: DIF stages */
    for (unsigned int s = 0; s < CHIPMUNK_FRI_NTT_LOG; ++s) {
        unsigned int l_size = 1u << (s + 1);    /* butterfly size: 2, 4, ..., N */
        unsigned int l_half = 1u << s;           /* half-size: 1, 2, ..., N/2 */
        unsigned int l_stride = 1u << (CHIPMUNK_FRI_NTT_LOG - 1 - s);

        for (unsigned int l_start = 0; l_start < CHIPMUNK_FRI_NTT_SIZE;
             l_start += l_size) {
            for (unsigned int j = 0; j < l_half; ++j) {
                unsigned int l_idx = l_start + j;
                int32_t l_zeta = s_fri_zetas[j * l_stride];
                int32_t l_t = s_fri_fqmul(l_zeta, a[l_idx + l_half]);
                int32_t l_u = a[l_idx];
                int32_t l_sum = l_u + l_t;
                int32_t l_dif = l_u - l_t;
                if (l_sum >= (int32_t)CHIPMUNK_Q) l_sum -= (int32_t)CHIPMUNK_Q;
                if (l_dif < 0)                     l_dif += (int32_t)CHIPMUNK_Q;
                a[l_idx]         = l_sum;
                a[l_idx + l_half] = l_dif;
            }
        }
    }
}

void chipmunk_fri_ntt_forward_q(int32_t a[CHIPMUNK_FRI_NTT_SIZE],
                                  const chipmunk_fri_ntt_ctx_t *ctx)
{
    uint64_t l_q = ctx->q;
    s_fri_bit_reverse(a);

    for (unsigned int s = 0; s < CHIPMUNK_FRI_NTT_LOG; ++s) {
        unsigned int l_size = 1u << (s + 1);
        unsigned int l_half = 1u << s;
        unsigned int l_stride = 1u << (CHIPMUNK_FRI_NTT_LOG - 1 - s);

        for (unsigned int l_start = 0; l_start < CHIPMUNK_FRI_NTT_SIZE;
             l_start += l_size) {
            for (unsigned int j = 0; j < l_half; ++j) {
                unsigned int l_idx = l_start + j;
                int32_t l_zeta = ctx->zetas[j * l_stride];
                int32_t l_t = s_fri_fqmul_q(l_zeta, a[l_idx + l_half], l_q);
                int32_t l_u = a[l_idx];
                int32_t l_sum = l_u + l_t;
                int32_t l_dif = l_u - l_t;
                if (l_sum >= (int32_t)l_q) l_sum -= (int32_t)l_q;
                if (l_dif < 0)             l_dif += (int32_t)l_q;
                a[l_idx]         = l_sum;
                a[l_idx + l_half] = l_dif;
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * Inverse NTT: Decimation-In-Time (DIT), in-place.
 *
 * Input:  a[k] = evaluations in natural order.
 * Output: a[i] = coefficients in natural order.
 *
 * Algorithm:
 *   1. DIT stages in reverse: s = logN-1..0
 *   2. Bit-reverse output
 *   3. Scale by N^{-1}
 * ------------------------------------------------------------------------- */

void chipmunk_fri_ntt_inverse(int32_t a[CHIPMUNK_FRI_NTT_SIZE])
{
    /* Step 1: DIT stages (reverse order) */
    for (int s = (int)CHIPMUNK_FRI_NTT_LOG - 1; s >= 0; --s) {
        unsigned int l_size = 1u << (s + 1);
        unsigned int l_half = 1u << s;
        unsigned int l_stride = 1u << (CHIPMUNK_FRI_NTT_LOG - 1 - s);

        for (unsigned int l_start = 0; l_start < CHIPMUNK_FRI_NTT_SIZE;
             l_start += l_size) {
            for (unsigned int j = 0; j < l_half; ++j) {
                unsigned int l_idx = l_start + j;
                int32_t l_zeta = s_fri_zetas_inv[j * l_stride];
                int32_t l_u = a[l_idx];
                int32_t l_v = a[l_idx + l_half];
                int32_t l_sum = l_u + l_v;
                int32_t l_dif = l_u - l_v;
                if (l_sum >= (int32_t)CHIPMUNK_Q) l_sum -= (int32_t)CHIPMUNK_Q;
                if (l_dif < 0)                     l_dif += (int32_t)CHIPMUNK_Q;
                a[l_idx]         = l_sum;
                a[l_idx + l_half] = s_fri_fqmul(l_zeta, l_dif);
            }
        }
    }

    /* Step 2: bit-reverse output */
    s_fri_bit_reverse(a);

    /* Step 3: scale by N^{-1} mod q */
    int32_t l_ninv = chipmunk_field_inv_2048();
    for (unsigned int i = 0; i < CHIPMUNK_FRI_NTT_SIZE; ++i) {
        a[i] = s_fri_fqmul(a[i], l_ninv);
    }
}

void chipmunk_fri_ntt_inverse_q(int32_t a[CHIPMUNK_FRI_NTT_SIZE],
                                  const chipmunk_fri_ntt_ctx_t *ctx)
{
    uint64_t l_q = ctx->q;

    for (int s = (int)CHIPMUNK_FRI_NTT_LOG - 1; s >= 0; --s) {
        unsigned int l_size = 1u << (s + 1);
        unsigned int l_half = 1u << s;
        unsigned int l_stride = 1u << (CHIPMUNK_FRI_NTT_LOG - 1 - s);

        for (unsigned int l_start = 0; l_start < CHIPMUNK_FRI_NTT_SIZE;
             l_start += l_size) {
            for (unsigned int j = 0; j < l_half; ++j) {
                unsigned int l_idx = l_start + j;
                int32_t l_zeta = ctx->zetas_inv[j * l_stride];
                int32_t l_u = a[l_idx];
                int32_t l_v = a[l_idx + l_half];
                int32_t l_sum = l_u + l_v;
                int32_t l_dif = l_u - l_v;
                if (l_sum >= (int32_t)l_q) l_sum -= (int32_t)l_q;
                if (l_dif < 0)             l_dif += (int32_t)l_q;
                a[l_idx]         = l_sum;
                a[l_idx + l_half] = s_fri_fqmul_q(l_zeta, l_dif, l_q);
            }
        }
    }

    s_fri_bit_reverse(a);

    for (unsigned int i = 0; i < CHIPMUNK_FRI_NTT_SIZE; ++i) {
        a[i] = s_fri_fqmul_q(a[i], ctx->inv_n, l_q);
    }
}

/* -------------------------------------------------------------------------
 * Coset NTT: evaluate f at shifted domain {g * omega^k}.
 *
 * Multiply coefficient a[i] by g^i, then forward NTT.
 * After NTT: a[k] = f(g * omega^k) for k = 0..N-1.
 * ------------------------------------------------------------------------- */

void chipmunk_fri_ntt_coset_forward(int32_t a[CHIPMUNK_FRI_NTT_SIZE],
                                    int32_t coset_g)
{
    if (coset_g == 1) {
        chipmunk_fri_ntt_forward(a);
        return;
    }

    /* Precompute powers of coset_g: g^0, g^1, ..., g^{N-1} */
    int32_t l_g_pow = 1;
    for (unsigned int i = 0; i < CHIPMUNK_FRI_NTT_SIZE; ++i) {
        a[i] = s_fri_fqmul(a[i], l_g_pow);
        l_g_pow = s_fri_fqmul(l_g_pow, coset_g);
    }

    chipmunk_fri_ntt_forward(a);
}

void chipmunk_fri_ntt_coset_forward_q(int32_t a[CHIPMUNK_FRI_NTT_SIZE],
                                        int32_t coset_g,
                                        const chipmunk_fri_ntt_ctx_t *ctx)
{
    uint64_t l_q = ctx->q;
    if (coset_g == 1) {
        chipmunk_fri_ntt_forward_q(a, ctx);
        return;
    }

    int32_t l_g_pow = 1;
    for (unsigned int i = 0; i < CHIPMUNK_FRI_NTT_SIZE; ++i) {
        a[i] = s_fri_fqmul_q(a[i], l_g_pow, l_q);
        l_g_pow = s_fri_fqmul_q(l_g_pow, coset_g, l_q);
    }

    chipmunk_fri_ntt_forward_q(a, ctx);
}

/* -------------------------------------------------------------------------
 * Domain generation
 * ------------------------------------------------------------------------- */

int32_t chipmunk_fri_ntt_omega(void)
{
    return chipmunk_field_omega_2048();
}

void chipmunk_fri_ntt_domain(int32_t domain[CHIPMUNK_FRI_NTT_SIZE])
{
    int32_t l_omega = chipmunk_field_omega_2048();
    int32_t l_val = 1;
    for (unsigned int i = 0; i < CHIPMUNK_FRI_NTT_SIZE; ++i) {
        domain[i] = l_val;
        l_val = s_fri_fqmul(l_val, l_omega);
    }
}

void chipmunk_fri_ntt_coset_domain(int32_t domain[CHIPMUNK_FRI_NTT_SIZE],
                                   int32_t coset_g)
{
    int32_t l_omega = chipmunk_field_omega_2048();
    int32_t l_val = coset_g;
    for (unsigned int i = 0; i < CHIPMUNK_FRI_NTT_SIZE; ++i) {
        domain[i] = l_val;
        l_val = s_fri_fqmul(l_val, l_omega);
    }
}
