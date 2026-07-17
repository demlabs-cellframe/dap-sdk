/*
 * chipmunk_field.c — Scalar field arithmetic for F_q (q = 3168257).
 *
 * See chipmunk_field.h for documentation.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "chipmunk_field.h"
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <errno.h>

#include "dap_common.h"

#define LOG_TAG "chipmunk_field"

/* -------------------------------------------------------------------------
 * Internal: safe modular reduction into [0, q)
 * ---------------------------------------------------------------------- */

/* Parameterized modular reduction into [0, q). */
static inline int32_t s_freduce_q(int64_t a_val, uint64_t q)
{
    int64_t l_r = a_val % (int64_t)q;
    if (l_r < 0) {
        l_r += (int64_t)q;
    }
    return (int32_t)l_r;
}

/* -------------------------------------------------------------------------
 * Modular inverse: a^{-1} = a^{q-2} mod q (Fermat's little theorem)
 *
 * Since q is prime and a ∈ [1, q-1], a^{q-1} ≡ 1 mod q, so
 * a^{-1} ≡ a^{q-2} mod q.
 *
 * For a = 0, returns 0 (not invertible).
 * ---------------------------------------------------------------------- */

/* Non-_q wrappers: delegate to _q variants with CHIPMUNK_Q. */
int32_t chipmunk_field_inv(int32_t a)
{
    return chipmunk_field_inv_q(a, (uint64_t)CHIPMUNK_Q);
}

int32_t chipmunk_field_pow(int32_t base, uint32_t exp)
{
    return chipmunk_field_pow_q(base, exp, (uint64_t)CHIPMUNK_Q);
}

int chipmunk_field_primitive_root_2k(uint32_t k, int32_t *out_omega)
{
    return chipmunk_field_primitive_root_2k_q(k, out_omega, (uint64_t)CHIPMUNK_Q);
}

/* Parameterized modular inverse via Fermat's little theorem. */
int32_t chipmunk_field_inv_q(int32_t a, uint64_t q)
{
    if (a <= 0 || a >= (int32_t)q) {
        a = s_freduce_q((int64_t)a, q);
    }
    if (a == 0) {
        return 0;
    }
    return chipmunk_field_pow_q(a, (uint32_t)q - 2u, q);
}

/* -------------------------------------------------------------------------
 * Modular exponentiation: binary square-and-multiply
 * ---------------------------------------------------------------------- */

/* Parameterized modular exponentiation: base^exp mod q. */
int32_t chipmunk_field_pow_q(int32_t base, uint32_t exp, uint64_t q)
{
    int64_t l_result = 1;
    int64_t l_b = s_freduce_q((int64_t)base, q);
    uint32_t l_e = exp;

    while (l_e) {
        if (l_e & 1u) {
            l_result = s_freduce_q(l_result * l_b, q);
        }
        l_b = s_freduce_q(l_b * l_b, q);
        l_e >>= 1;
    }

    return (int32_t)l_result;
}

/* -------------------------------------------------------------------------
 * Find primitive 2^k-th root of unity
 *
 * Algorithm:
 *   1. Check k ≤ 2-adicity(q-1) = CHIPMUNK_FIELD_TWO_ADICITY
 *   2. exp = (q-1) / 2^k
 *   3. For g = 2, 3, ..., 99: compute omega = g^exp mod q
 *   4. Verify omega^{2^{k-1}} ≡ -1 (mod q)  — proves exact order 2^k
 * ---------------------------------------------------------------------- */

/* Parameterized primitive root of order 2^k in F_q.
 * Caller must ensure 2^k | (q-1) (checked via two-adicity elsewhere). */
int chipmunk_field_primitive_root_2k_q(uint32_t k, int32_t *out_omega, uint64_t q)
{
    if (!out_omega || k == 0) {
        return -1;
    }
    /* k_max = floor(log2(q-1)); no hard cap for arbitrary q. */
    if (k >= 32) {
        return -1;
    }

    /* exp = (q - 1) / 2^k */
    uint64_t l_exp = (q - 1u) >> k;

    for (uint32_t l_g = 2; l_g < 100; ++l_g) {
        /* pow_q takes uint32_t exp; (q-1)/2^k < q < 2^32 for our primes, OK */
        int32_t l_o = chipmunk_field_pow_q((int32_t)l_g, (uint32_t)l_exp, q);
        if (l_o <= 1) {
            continue;
        }
        int32_t l_half = chipmunk_field_pow_q(l_o, 1u << (k - 1), q);
        if (l_half == (int32_t)q - 1) {
            *out_omega = l_o;
            return 0;
        }
    }

    log_it(L_ERROR, "chipmunk_field: no primitive 2^%u-th root of unity found (q=%lu)",
           k, (unsigned long)q);
    return -1;
}

/* -------------------------------------------------------------------------
 * Lazy-initialised global constants for the FRI domain
 * ---------------------------------------------------------------------- */

typedef struct field_constants {
    int32_t omega_2048;
    int32_t omega_2048_inv;
    int32_t omega_512;
    int32_t omega_512_inv;
    int32_t inv_2048;
    int32_t inv_512;
} field_constants_t;

static field_constants_t g_field_consts;
static atomic_int g_field_state = 0;  /* 0=uninit, 1=initing, 2=ready */
static pthread_mutex_t g_field_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Check that g^((q-1)/2^k) where g=generator and k=two_adicity
 * produces a full-order element. */
static int s_field_init_locked(void)
{
    int l_rc;
    const uint64_t l_q = (uint64_t)CHIPMUNK_Q;

    /* omega_2048: primitive 2048-th root of unity */
    l_rc = chipmunk_field_primitive_root_2k_q(11, &g_field_consts.omega_2048, l_q);
    if (l_rc != 0) {
        log_it(L_CRITICAL, "chipmunk_field: failed to find omega_2048");
        return l_rc;
    }

    /* omega_2048^{-1} = omega_2048^{2047} */
    g_field_consts.omega_2048_inv = chipmunk_field_pow_q(
        g_field_consts.omega_2048, CHIPMUNK_FIELD_FRI_DOMAIN - 1u, l_q);

    /* omega_512: primitive 512-th root = omega_2048^{4} */
    g_field_consts.omega_512 = chipmunk_field_pow_q(g_field_consts.omega_2048, 4, l_q);

    /* omega_512^{-1} = omega_512^{511} */
    g_field_consts.omega_512_inv = chipmunk_field_pow_q(
        g_field_consts.omega_512, CHIPMUNK_N - 1u, l_q);

    /* N^{-1} mod q */
    g_field_consts.inv_2048 = chipmunk_field_pow_q(
        (int32_t)CHIPMUNK_FIELD_FRI_DOMAIN, (uint32_t)CHIPMUNK_Q - 2u, l_q);
    g_field_consts.inv_512 = chipmunk_field_pow_q(
        (int32_t)CHIPMUNK_N, (uint32_t)CHIPMUNK_Q - 2u, l_q);

    /* Verification: omega^{2048} must be 1 */
    int32_t l_check = chipmunk_field_pow_q(g_field_consts.omega_2048, CHIPMUNK_FIELD_FRI_DOMAIN, l_q);
    if (l_check != 1) {
        log_it(L_CRITICAL, "chipmunk_field: omega_2048^2048 = %d (expected 1)", l_check);
        return -1;
    }

    /* Verification: omega^{1024} must be -1 */
    l_check = chipmunk_field_pow_q(g_field_consts.omega_2048, CHIPMUNK_FIELD_FRI_DOMAIN / 2, l_q);
    if (l_check != (int32_t)CHIPMUNK_Q - 1) {
        log_it(L_CRITICAL, "chipmunk_field: omega_2048^1024 = %d (expected q-1)", l_check);
        return -1;
    }

    /* Verification: omega * omega_inv == 1 */
    l_check = s_freduce_q((int64_t)g_field_consts.omega_2048 *
                          (int64_t)g_field_consts.omega_2048_inv, l_q);
    if (l_check != 1) {
        log_it(L_CRITICAL, "chipmunk_field: omega * omega_inv = %d (expected 1)", l_check);
        return -1;
    }

    log_it(L_DEBUG, "chipmunk_field: initialised — omega_2048=%d, omega_512=%d, "
           "inv_2048=%d, inv_512=%d",
           g_field_consts.omega_2048, g_field_consts.omega_512,
           g_field_consts.inv_2048, g_field_consts.inv_512);
    return 0;
}

int chipmunk_field_init(void)
{
    int l_state = atomic_load_explicit(&g_field_state, memory_order_acquire);
    if (l_state == 2) {
        return 0;
    }

    pthread_mutex_lock(&g_field_mutex);
    l_state = atomic_load_explicit(&g_field_state, memory_order_acquire);
    if (l_state == 2) {
        pthread_mutex_unlock(&g_field_mutex);
        return 0;
    }

    int l_rc = s_field_init_locked();
    if (l_rc == 0) {
        atomic_store_explicit(&g_field_state, 2, memory_order_release);
    }
    pthread_mutex_unlock(&g_field_mutex);
    return l_rc;
}

bool chipmunk_field_is_initialized(void)
{
    return atomic_load_explicit(&g_field_state, memory_order_acquire) == 2;
}

int32_t chipmunk_field_omega_2048(void)
{
    return g_field_consts.omega_2048;
}

int32_t chipmunk_field_omega_2048_inv(void)
{
    return g_field_consts.omega_2048_inv;
}

int32_t chipmunk_field_omega_512(void)
{
    return g_field_consts.omega_512;
}

int32_t chipmunk_field_omega_512_inv(void)
{
    return g_field_consts.omega_512_inv;
}

int32_t chipmunk_field_inv_2048(void)
{
    return g_field_consts.inv_2048;
}

int32_t chipmunk_field_inv_512(void)
{
    return g_field_consts.inv_512;
}

/* -------------------------------------------------------------------------
 * Per-q field constants calculator (Phase 9.13)
 *
 * Computes the FRI-domain roots of unity and inverses for an arbitrary
 * prime modulus q (not just the global CHIPMUNK_Q). The caller owns the
 * output struct; no global state is touched.
 * ---------------------------------------------------------------------- */

int chipmunk_field_compute_for_q(chipmunk_field_consts_t *a_out,
                                   uint64_t q, uint32_t two_adicity)
{
    if (!a_out) return -EINVAL;
    if (q == 0 || (q & 1u) == 0) return -EINVAL;
    if (two_adicity == 0 || two_adicity >= 32) return -EINVAL;

    /* omega = primitive 2^two_adicity-th root of unity. */
    int32_t l_omega;
    int l_rc = chipmunk_field_primitive_root_2k_q(two_adicity, &l_omega, q);
    if (l_rc != 0) {
        log_it(L_ERROR, "chipmunk_field: no primitive 2^%u root for q=%lu",
               two_adicity, (unsigned long)q);
        return l_rc;
    }
    uint32_t l_domain = 1u << two_adicity;

    a_out->q = q;
    a_out->two_adicity = two_adicity;
    a_out->omega = l_omega;
    a_out->omega_inv = chipmunk_field_pow_q(l_omega, l_domain - 1u, q);
    a_out->inv_domain = chipmunk_field_pow_q((int32_t)l_domain,
                                              (uint32_t)q - 2u, q);

    /* Verify omega^domain == 1. */
    int32_t l_check = chipmunk_field_pow_q(l_omega, l_domain, q);
    if (l_check != 1) {
        log_it(L_CRITICAL, "chipmunk_field: omega^%u = %d (expected 1) for q=%lu",
               l_domain, l_check, (unsigned long)q);
        return -1;
    }
    /* Verify omega^(domain/2) == q-1 (-1). */
    l_check = chipmunk_field_pow_q(l_omega, l_domain / 2u, q);
    if (l_check != (int32_t)q - 1) {
        log_it(L_CRITICAL, "chipmunk_field: omega^%u = %d (expected q-1) for q=%lu",
               l_domain / 2u, l_check, (unsigned long)q);
        return -1;
    }
    /* Verify omega * omega_inv == 1. */
    l_check = s_freduce_q((int64_t)a_out->omega * (int64_t)a_out->omega_inv, q);
    if (l_check != 1) {
        log_it(L_CRITICAL, "chipmunk_field: omega*omega_inv = %d (expected 1) for q=%lu",
               l_check, (unsigned long)q);
        return -1;
    }

    log_it(L_DEBUG, "chipmunk_field: per-q consts q=%lu 2ad=%u omega=%d omega_inv=%d inv_dom=%d",
           (unsigned long)q, two_adicity, a_out->omega, a_out->omega_inv, a_out->inv_domain);
    return 0;
}
