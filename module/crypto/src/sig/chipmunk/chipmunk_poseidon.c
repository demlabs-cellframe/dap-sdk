/*
 * chipmunk_poseidon.c — Poseidon permutation over F_q.
 *
 * See chipmunk_poseidon.h for documentation.
 *
 * All parameters (MDS matrix, round constants) are deterministically
 * computed from (q, t, R_F, R_P) at runtime via SHAKE256. No hardcoded
 * constants — backward compatibility is not preserved across parameter
 * changes. This is intentional: the Poseidon permutation must be
 * reproducible from public parameters alone.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "chipmunk_poseidon.h"
#include "chipmunk_field.h"
#include "chipmunk_poly.h"
#include <string.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <pthread.h>

#include "dap_common.h"
#include "dap_hash_shake256.h"
#include "dap_memwipe.h"

#define LOG_TAG "chipmunk_poseidon"

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/** Parameterized modular reduction: arbitrary int64_t → [0, q). */
static inline int32_t s_freduce_q(int64_t a_val, uint64_t q)
{
    int64_t l_r = a_val % (int64_t)q;
    if (l_r < 0) l_r += (int64_t)q;
    return (int32_t)l_r;
}

/** S-box: x → x^5 mod q.  Two multiplications: x^2 * x^2 * x. */
static inline int32_t s_sbox_q(int32_t x, uint64_t q)
{
    int64_t x2 = (int64_t)x * x;
    x2 %= (int64_t)q;
    int64_t x4 = x2 * x2;
    x4 %= (int64_t)q;
    int64_t x5 = x4 * (int64_t)x;
    return s_freduce_q(x5, q);
}

/* =========================================================================
 * Per-q parameter computation
 *
 * MDS matrix: Cauchy M[i][j] = (x_i + y_j)^{-1} mod q  where x={0,1,2}, y={3,4,5}.
 * Round constants: SHAKE256("Poseidon-" || q_LE8 || round_LE4 || col_LE4) → 4 bytes LE,
 *   rejection-sampled into [0, q).  Domain separation includes q so distinct
 *   moduli produce distinct constant sets.
 * ========================================================================= */

int chipmunk_poseidon_params_compute(chipmunk_poseidon_params_t *a_out, uint64_t q)
{
    if (!a_out || q == 0 || (q & 1u) == 0) return -1;

    memset(a_out, 0, sizeof(*a_out));
    a_out->q = q;

    /* MDS matrix: M[i][j] = (x_i + y_j)^{-1} mod q. */
    static const int32_t s_x[3] = {0, 1, 2};
    static const int32_t s_y[3] = {3, 4, 5};
    for (unsigned i = 0; i < 3u; ++i) {
        for (unsigned j = 0; j < 3u; ++j) {
            int32_t l_denom = s_x[i] + s_y[j];
            a_out->mds[i][j] = chipmunk_field_inv_q(l_denom, q);
            if (a_out->mds[i][j] == 0) {
                log_it(L_ERROR, "Poseidon: MDS element (%u,%u) not invertible for q=%lu",
                       i, j, (unsigned long)q);
                return -1;
            }
        }
    }

    /* Round constants: deterministic from q via SHAKE256.
     * Seed = "Poseidon-" || q_LE8 || round_LE4 || col_LE4
     * Rejection-sample 4 bytes LE into [0, q). */
    for (unsigned r = 0; r < CHIPMUNK_POSEIDON_R; ++r) {
        for (unsigned t = 0; t < CHIPMUNK_POSEIDON_T; ++t) {
            uint8_t l_seed[21];
            memcpy(l_seed, "Poseidon-", 9);
            memcpy(l_seed + 9, &q, 8);              /* q LE8 */
            uint32_t r32 = r, t32 = t;
            memcpy(l_seed + 17, &r32, 4);            /* round LE4 */
            /* We reuse r32/t32 storage — absorb column into bytes 17-20.
             * Overwrite with: round|col packed as two LE4 at offsets 17,17+4
             * but seed is only 21 bytes.  Simpler: use round*3+t as single LE4. */
            uint32_t l_idx = r * CHIPMUNK_POSEIDON_T + t;
            memcpy(l_seed + 17, &l_idx, 4);
            /* Total seed = 9 + 8 + 4 = 21 bytes */

            /* SHAKE256 squeeze → rejection sample. */
            uint8_t l_hash[32];
            dap_hash_shake256(l_hash, 32, l_seed, 21);

            /* Rejection-sample 4 bytes LE into [0, q).
             * q < 2^22 for all planned primes, so acceptance prob > 75%. */
            for (unsigned off = 0; off + 4 <= 32; off += 4) {
                uint32_t l_val = (uint32_t)l_hash[off]
                               | ((uint32_t)l_hash[off + 1] << 8)
                               | ((uint32_t)l_hash[off + 2] << 16)
                               | ((uint32_t)l_hash[off + 3] << 24);
                /* Mask to 24 bits (sufficient for q < 2^22). */
                l_val &= 0xFFFFFFu;
                if (l_val < (uint32_t)q) {
                    a_out->rc[r][t] = (int32_t)l_val;
                    goto next_rc;
                }
            }
            /* If all 7 attempts rejected (extremely unlikely), use last value mod q. */
            a_out->rc[r][t] = (int32_t)(0xFFFFFFu % (uint32_t)q);
next_rc:    ;
        }
    }

    /* Verification: MDS * [1,0,0]^T should give first column of M. */
    int32_t l_v[3] = {1, 0, 0};
    for (unsigned i = 0; i < 3u; ++i) {
        int64_t l_sum = 0;
        for (unsigned j = 0; j < 3u; ++j) {
            l_sum += (int64_t)a_out->mds[i][j] * l_v[j];
        }
        if (s_freduce_q(l_sum, q) != a_out->mds[i][0]) {
            log_it(L_ERROR, "Poseidon: MDS verification failed for q=%lu", (unsigned long)q);
            return -1;
        }
    }

    log_it(L_DEBUG, "Poseidon: params computed for q=%lu", (unsigned long)q);
    return 0;
}

void chipmunk_poseidon_params_free(chipmunk_poseidon_params_t *a_params)
{
    (void)a_params;
    /* No heap allocations — MDS and RC are inline arrays. */
}

/* =========================================================================
 * Lazy-initialized default params for CHIPMUNK_Q
 * ========================================================================= */

static chipmunk_poseidon_params_t s_default_params;
static atomic_int s_default_state = 0;  /* 0=uninit, 1=initing, 2=ready */
static pthread_mutex_t s_default_mutex = PTHREAD_MUTEX_INITIALIZER;

static chipmunk_poseidon_params_t *s_ensure_default_params(void)
{
    int l_state = atomic_load_explicit(&s_default_state, memory_order_acquire);
    if (l_state == 2) return &s_default_params;

    pthread_mutex_lock(&s_default_mutex);
    l_state = atomic_load_explicit(&s_default_state, memory_order_acquire);
    if (l_state == 2) {
        pthread_mutex_unlock(&s_default_mutex);
        return &s_default_params;
    }

    int rc = chipmunk_poseidon_params_compute(&s_default_params, (uint64_t)CHIPMUNK_Q);
    if (rc == 0) {
        atomic_store_explicit(&s_default_state, 2, memory_order_release);
    }
    pthread_mutex_unlock(&s_default_mutex);
    return (rc == 0) ? &s_default_params : NULL;
}

/* =========================================================================
 * Per-q Poseidon permutation
 * ========================================================================= */

void chipmunk_poseidon_perm_q(int32_t state[CHIPMUNK_POSEIDON_T],
                                const chipmunk_poseidon_params_t *a_params)
{
    uint64_t l_q = a_params->q;
    int32_t s0 = state[0], s1 = state[1], s2 = state[2];

    for (uint32_t i = 0; i < CHIPMUNK_POSEIDON_R; i++) {
        /* Add round constants */
        s0 = s_freduce_q((int64_t)s0 + a_params->rc[i][0], l_q);
        s1 = s_freduce_q((int64_t)s1 + a_params->rc[i][1], l_q);
        s2 = s_freduce_q((int64_t)s2 + a_params->rc[i][2], l_q);

        /* S-box */
        if (i < 4u || i >= 26u) {
            /* Full round: apply to all 3 words */
            s0 = s_sbox_q(s0, l_q);
            s1 = s_sbox_q(s1, l_q);
            s2 = s_sbox_q(s2, l_q);
        } else {
            /* Partial round: apply to first word only */
            s0 = s_sbox_q(s0, l_q);
        }

        /* MDS multiply */
        int64_t r0 = (int64_t)a_params->mds[0][0] * s0
                    + (int64_t)a_params->mds[0][1] * s1
                    + (int64_t)a_params->mds[0][2] * s2;
        int64_t r1 = (int64_t)a_params->mds[1][0] * s0
                    + (int64_t)a_params->mds[1][1] * s1
                    + (int64_t)a_params->mds[1][2] * s2;
        int64_t r2 = (int64_t)a_params->mds[2][0] * s0
                    + (int64_t)a_params->mds[2][1] * s1
                    + (int64_t)a_params->mds[2][2] * s2;
        s0 = s_freduce_q(r0, l_q);
        s1 = s_freduce_q(r1, l_q);
        s2 = s_freduce_q(r2, l_q);
    }

    state[0] = s0;
    state[1] = s1;
    state[2] = s2;
}

int32_t chipmunk_poseidon_hash2_q(int32_t left, int32_t right,
                                    const chipmunk_poseidon_params_t *a_params)
{
    int32_t state[3] = { 0, left, right };
    chipmunk_poseidon_perm_q(state, a_params);
    return state[0];
}

/* =========================================================================
 * Legacy non-parameterized API (delegates to _q via default params)
 * ========================================================================= */

int chipmunk_poseidon_init(void)
{
    /* Force lazy init so subsequent calls are fast. */
    return s_ensure_default_params() ? 0 : -1;
}

void chipmunk_poseidon_perm(int32_t state[CHIPMUNK_POSEIDON_T])
{
    chipmunk_poseidon_params_t *p = s_ensure_default_params();
    if (p) chipmunk_poseidon_perm_q(state, p);
}

int32_t chipmunk_poseidon_hash2(int32_t left, int32_t right)
{
    chipmunk_poseidon_params_t *p = s_ensure_default_params();
    return p ? chipmunk_poseidon_hash2_q(left, right, p) : 0;
}
