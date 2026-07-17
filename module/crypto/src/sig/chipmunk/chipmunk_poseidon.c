/*
 * chipmunk_poseidon.c — Poseidon permutation over F_q (q = 3168257).
 *
 * See chipmunk_poseidon.h for documentation.
 *
 * Implementation notes:
 *   - All constants are compile-time (static const arrays).
 *   - S-box x^5: computed inline via two multiplications (x^2 * x^2 * x).
 *   - MDS multiply: manually unrolled 3x3 matrix-vector product.
 *   - No heap allocation, no dynamic state.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "chipmunk_poseidon.h"
#include "chipmunk_field.h"
#include "chipmunk_poly.h"
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "dap_common.h"
#include "dap_hash_shake256.h"
#include "dap_memwipe.h"

#define LOG_TAG "chipmunk_poseidon"

/* Lazily-initialized default params for CHIPMUNK_Q.
 * Defined below, after s_mds and s_rc arrays. */
static chipmunk_poseidon_params_t s_default_params;
static bool s_default_params_init = false;
static void s_ensure_default_params(void);

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/* Forward declarations for _q variants (defined in Phase 9.14g section). */
static inline int32_t s_freduce_q(int64_t a_val, uint64_t q);
static inline int32_t s_sbox_q(int32_t x, uint64_t q);

/** Safe modular reduction: arbitrary int64_t → [0, q).
 *  Works for any a_val (uses % operator, handles negatives). */
static inline int32_t s_freduce(int64_t a_val)
{
    return s_freduce_q(a_val, (uint64_t)CHIPMUNK_Q);
}

/** S-box: x → x^5 mod q.  Two multiplications: x^2 * x^2 * x. */
static inline int32_t s_sbox(int32_t x)
{
    return s_sbox_q(x, (uint64_t)CHIPMUNK_Q);
}

/* =========================================================================
 * MDS matrix (3x3 Cauchy over F_q)
 *
 * M[i][j] = 1 / (x_i + y_j) where x = {0,1,2}, y = {3,4,5}.
 * All 2x2 subdeterminants are nonzero (MDS property verified).
 * ========================================================================= */

static const int32_t s_mds[3][3] = {
    { 1056086, 2376193, 1267303 },
    { 2376193, 1267303,  528043 },
    { 1267303,  528043, 2715649 }
};

/* =========================================================================
 * Round constants
 *
 * Generated via SHAKE256("ChipmunkPoseidon-FRI-Poseidon1" || counter)
 * with rejection sampling into [0, q).
 *
 * Rounds 0-3: full (R_F/2 = 4)
 * Rounds 4-25: partial (R_P = 22)
 * Rounds 26-29: full (R_F/2 = 4)
 * ========================================================================= */

static const int32_t s_rc[CHIPMUNK_POSEIDON_R][CHIPMUNK_POSEIDON_T] = {
    /* Round  0 (FULL) */ {  615815, 1622222, 2339704 },
    /* Round  1 (FULL) */ { 1695599, 1442386, 2211767 },
    /* Round  2 (FULL) */ {  560177, 1406585, 1356493 },
    /* Round  3 (FULL) */ { 2364876, 1651330, 1056974 },
    /* Round  4 (PART) */ {  449911, 3109418, 1829300 },
    /* Round  5 (PART) */ { 1725222, 1466820, 1201373 },
    /* Round  6 (PART) */ {  921092, 1152106, 1254661 },
    /* Round  7 (PART) */ {  603878, 1412150,  471566 },
    /* Round  8 (PART) */ {  788386, 1494784, 2694927 },
    /* Round  9 (PART) */ {  76995, 1757311, 2720061 },
    /* Round 10 (PART) */ {  436859, 1880034,  807378 },
    /* Round 11 (PART) */ {  530875, 1607825, 1273122 },
    /* Round 12 (PART) */ { 2566206, 2518201, 2947239 },
    /* Round 13 (PART) */ { 2835451, 3123784, 2401287 },
    /* Round 14 (PART) */ {  226494, 2460184, 2923118 },
    /* Round 15 (PART) */ {  318576, 1670768,  960379 },
    /* Round 16 (PART) */ { 1354081,  419386, 1526559 },
    /* Round 17 (PART) */ { 1678400, 1168385, 2313555 },
    /* Round 18 (PART) */ { 2352122, 1753711, 2535506 },
    /* Round 19 (PART) */ { 1487508, 1001327,   54162 },
    /* Round 20 (PART) */ { 3079796, 1019449, 1810348 },
    /* Round 21 (PART) */ {  265036, 2529083,  910265 },
    /* Round 22 (PART) */ { 1177450,  895745, 2432887 },
    /* Round 23 (PART) */ { 2113013, 1014921, 2034609 },
    /* Round 24 (PART) */ { 2187472, 3009498, 1283849 },
    /* Round 25 (PART) */ { 1974048,  613252,  308479 },
    /* Round 26 (FULL) */ {  789998,  765647, 2328325 },
    /* Round 27 (FULL) */ { 2485571, 1176958, 2828467 },
    /* Round 28 (FULL) */ {  199072, 1886084, 1822673 },
    /* Round 29 (FULL) */ { 1530088,  660968, 1675463 }
};

/* Implementation of s_ensure_default_params (forward-declared above).
 * Uses the original hardcoded MDS and round constants rather than
 * recomputing them (the generation was one-time and the exact values
 * must match for backward compatibility with existing proofs). */
static void s_ensure_default_params(void)
{
    if (!s_default_params_init) {
        memset(&s_default_params, 0, sizeof(s_default_params));
        s_default_params.q = (uint64_t)CHIPMUNK_Q;
        for (unsigned i = 0; i < 3u; ++i)
            for (unsigned j = 0; j < 3u; ++j)
                s_default_params.mds[i][j] = s_mds[i][j];
        for (unsigned r = 0; r < CHIPMUNK_POSEIDON_R; ++r)
            for (unsigned t = 0; t < CHIPMUNK_POSEIDON_T; ++t)
                s_default_params.rc[r][t] = s_rc[r][t];
        s_default_params_init = true;
    }
}

/* =========================================================================
 * MDS matrix-vector multiply (manually unrolled for 3x3)
 *
 * out[i] = sum_j M[i][j] * state[j] mod q
 * ========================================================================= */

static inline void s_mds_mul(const int32_t state[3], int32_t out[3])
{
    int64_t s0, s1, s2;

    s0 = (int64_t)s_mds[0][0] * state[0]
       + (int64_t)s_mds[0][1] * state[1]
       + (int64_t)s_mds[0][2] * state[2];
    s1 = (int64_t)s_mds[1][0] * state[0]
       + (int64_t)s_mds[1][1] * state[1]
       + (int64_t)s_mds[1][2] * state[2];
    s2 = (int64_t)s_mds[2][0] * state[0]
       + (int64_t)s_mds[2][1] * state[1]
       + (int64_t)s_mds[2][2] * state[2];

    out[0] = s_freduce(s0);
    out[1] = s_freduce(s1);
    out[2] = s_freduce(s2);
}

/* =========================================================================
 * Public API
 * ========================================================================= */

int chipmunk_poseidon_init(void)
{
    /* No dynamic state — all constants are compile-time. */
    return 0;
}

void chipmunk_poseidon_perm(int32_t state[CHIPMUNK_POSEIDON_T])
{
    s_ensure_default_params();
    chipmunk_poseidon_perm_q(state, &s_default_params);
}

int32_t chipmunk_poseidon_hash2(int32_t left, int32_t right)
{
    s_ensure_default_params();
    return chipmunk_poseidon_hash2_q(left, right, &s_default_params);
}

/* =========================================================================
 * Phase 9.14g: Per-q Poseidon
 * ========================================================================= */

/* Parameterized modular reduction. */
static inline int32_t s_freduce_q(int64_t a_val, uint64_t q)
{
    int64_t l_r = a_val % (int64_t)q;
    if (l_r < 0) l_r += (int64_t)q;
    return (int32_t)l_r;
}

/* Parameterized S-box: x → x^5 mod q. */
static inline int32_t s_sbox_q(int32_t x, uint64_t q)
{
    int64_t x2 = (int64_t)x * x;
    x2 %= (int64_t)q;
    int64_t x4 = x2 * x2;
    x4 %= (int64_t)q;
    int64_t x5 = x4 * (int64_t)x;
    return s_freduce_q(x5, q);
}

int chipmunk_poseidon_params_compute(chipmunk_poseidon_params_t *a_out, uint64_t q)
{
    if (!a_out || q == 0 || (q & 1u) == 0) return -1;

    memset(a_out, 0, sizeof(*a_out));
    a_out->q = q;

    /* Build Cauchy MDS matrix: M[i][j] = (x_i + y_j)^{-1} mod q.
     * x = {0, 1, 2}, y = {3, 4, 5}. */
    static const int32_t s_x[3] = {0, 1, 2};
    static const int32_t s_y[3] = {3, 4, 5};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            int32_t l_denom = s_x[i] + s_y[j];
            a_out->mds[i][j] = chipmunk_field_inv_q(l_denom, q);
            if (a_out->mds[i][j] == 0) {
                log_it(L_ERROR, "Poseidon: MDS element (%d,%d) not invertible for q=%lu",
                       i, j, (unsigned long)q);
                return -1;
            }
        }
    }

    /* Generate round constants via SHAKE256.
     * Domain: "ChipmunkPoseidon-FRI-Poseidon1" || counter (4 bytes LE).
     * Rejection-sample each value into [0, q). */
    const char *l_domain = "ChipmunkPoseidon-FRI-Poseidon1";
    const size_t l_domain_len = strlen(l_domain);

    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));
    dap_hash_shake256_absorb(l_state, (const uint8_t *)l_domain, l_domain_len);

    uint8_t l_block[DAP_SHAKE256_RATE];
    size_t l_pos = DAP_SHAKE256_RATE;  /* force squeeze on first use */
    size_t l_avail = 0;

    for (uint32_t r = 0; r < CHIPMUNK_POSEIDON_R; ++r) {
        for (uint32_t t = 0; t < CHIPMUNK_POSEIDON_T; ++t) {
            /* Rejection-sample 3 bytes → [0, q).
             * q < 2^22 for our primes, so 3 bytes (24 bits) suffice. */
            for (;;) {
                if (l_pos + 3 > l_avail) {
                    dap_hash_shake256_squeezeblocks(l_block, 1, l_state);
                    l_pos = 0;
                    l_avail = DAP_SHAKE256_RATE;
                }
                uint32_t l_val = (uint32_t)l_block[l_pos]
                               | ((uint32_t)l_block[l_pos + 1] << 8)
                               | ((uint32_t)l_block[l_pos + 2] << 16);
                l_pos += 3;
                if (l_val < (uint32_t)q) {
                    a_out->rc[r][t] = (int32_t)l_val;
                    break;
                }
            }
        }
    }

    dap_memwipe(l_block, sizeof(l_block));
    dap_memwipe(l_state, sizeof(l_state));

    log_it(L_DEBUG, "Poseidon: per-q params computed for q=%lu", (unsigned long)q);
    return 0;
}

void chipmunk_poseidon_params_free(chipmunk_poseidon_params_t *a_params)
{
    (void)a_params;
    /* MDS and RC are inline arrays — nothing to free. */
}

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
            s0 = s_sbox_q(s0, l_q);
            s1 = s_sbox_q(s1, l_q);
            s2 = s_sbox_q(s2, l_q);
        } else {
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
