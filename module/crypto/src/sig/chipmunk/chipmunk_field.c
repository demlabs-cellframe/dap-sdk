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

#include "dap_common.h"

#define LOG_TAG "chipmunk_field"

/* -------------------------------------------------------------------------
 * Internal: safe modular reduction into [0, q)
 * ---------------------------------------------------------------------- */

static inline int32_t s_freduce(int64_t a_val)
{
    int64_t l_r = a_val % (int64_t)CHIPMUNK_Q;
    if (l_r < 0) {
        l_r += (int64_t)CHIPMUNK_Q;
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

int32_t chipmunk_field_inv(int32_t a)
{
    if (a <= 0 || a >= (int32_t)CHIPMUNK_Q) {
        /* Canonicalise first */
        a = s_freduce((int64_t)a);
    }
    if (a == 0) {
        return 0;
    }
    return chipmunk_field_pow(a, (uint32_t)CHIPMUNK_Q - 2u);
}

/* -------------------------------------------------------------------------
 * Modular exponentiation: binary square-and-multiply
 * ---------------------------------------------------------------------- */

int32_t chipmunk_field_pow(int32_t base, uint32_t exp)
{
    /* Canonicalise base into [0, q) */
    int64_t l_result = 1;
    int64_t l_b = s_freduce((int64_t)base);
    uint32_t l_e = exp;

    while (l_e) {
        if (l_e & 1u) {
            l_result = s_freduce(l_result * l_b);
        }
        l_b = s_freduce(l_b * l_b);
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

int chipmunk_field_primitive_root_2k(uint32_t k, int32_t *out_omega)
{
    if (!out_omega || k == 0 || k > CHIPMUNK_FIELD_TWO_ADICITY) {
        return -1;
    }

    /* exp = (q - 1) / 2^k */
    uint32_t l_exp = ((uint32_t)CHIPMUNK_Q - 1u) >> k;

    for (uint32_t l_g = 2; l_g < 100; ++l_g) {
        int32_t l_o = chipmunk_field_pow((int32_t)l_g, l_exp);
        if (l_o <= 1) {
            continue;
        }

        /*
         * Verify exact order 2^k: omega^{2^{k-1}} must equal q-1 (i.e. -1).
         * If omega^{2^k} = 1 and omega^{2^{k-1}} = -1, the order is exactly 2^k.
         */
        int32_t l_half = chipmunk_field_pow(l_o, 1u << (k - 1));
        if (l_half == (int32_t)CHIPMUNK_Q - 1) {
            *out_omega = l_o;
            return 0;
        }
    }

    log_it(L_ERROR, "chipmunk_field: no primitive 2^%u-th root of unity found (q=%u)",
           k, (unsigned)CHIPMUNK_Q);
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

    /* omega_2048: primitive 2048-th root of unity */
    l_rc = chipmunk_field_primitive_root_2k(11, &g_field_consts.omega_2048);
    if (l_rc != 0) {
        log_it(L_CRITICAL, "chipmunk_field: failed to find omega_2048");
        return l_rc;
    }

    /* omega_2048^{-1} = omega_2048^{2047} */
    g_field_consts.omega_2048_inv = chipmunk_field_pow(
        g_field_consts.omega_2048, CHIPMUNK_FIELD_FRI_DOMAIN - 1u);

    /* omega_512: primitive 512-th root = omega_2048^{4} */
    g_field_consts.omega_512 = chipmunk_field_pow(g_field_consts.omega_2048, 4);

    /* omega_512^{-1} = omega_512^{511} */
    g_field_consts.omega_512_inv = chipmunk_field_pow(
        g_field_consts.omega_512, CHIPMUNK_N - 1u);

    /* N^{-1} mod q */
    g_field_consts.inv_2048 = chipmunk_field_pow(
        (int32_t)CHIPMUNK_FIELD_FRI_DOMAIN, (uint32_t)CHIPMUNK_Q - 2u);
    g_field_consts.inv_512 = chipmunk_field_pow(
        (int32_t)CHIPMUNK_N, (uint32_t)CHIPMUNK_Q - 2u);

    /* Verification: omega^{2048} must be 1 */
    int32_t l_check = chipmunk_field_pow(g_field_consts.omega_2048, CHIPMUNK_FIELD_FRI_DOMAIN);
    if (l_check != 1) {
        log_it(L_CRITICAL, "chipmunk_field: omega_2048^2048 = %d (expected 1)", l_check);
        return -1;
    }

    /* Verification: omega^{1024} must be -1 */
    l_check = chipmunk_field_pow(g_field_consts.omega_2048, CHIPMUNK_FIELD_FRI_DOMAIN / 2);
    if (l_check != (int32_t)CHIPMUNK_Q - 1) {
        log_it(L_CRITICAL, "chipmunk_field: omega_2048^1024 = %d (expected q-1)", l_check);
        return -1;
    }

    /* Verification: omega * omega_inv == 1 */
    l_check = s_freduce((int64_t)g_field_consts.omega_2048 *
                          (int64_t)g_field_consts.omega_2048_inv);
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
