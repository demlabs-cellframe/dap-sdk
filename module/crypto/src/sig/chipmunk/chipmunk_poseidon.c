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
#include <string.h>

#include "dap_common.h"

#define LOG_TAG "chipmunk_poseidon"

/* =========================================================================
 * Internal helpers
 * ========================================================================= */

/** Safe modular reduction: arbitrary int64_t → [0, q).
 *  Works for any a_val (uses % operator, handles negatives). */
static inline int32_t s_freduce(int64_t a_val)
{
    int64_t l_r = a_val % (int64_t)CHIPMUNK_Q;
    if (l_r < 0)
        l_r += (int64_t)CHIPMUNK_Q;
    return (int32_t)l_r;
}

/** S-box: x → x^5 mod q.  Two multiplications: x^2 * x^2 * x. */
static inline int32_t s_sbox(int32_t x)
{
    int64_t x2 = (int64_t)x * x;
    x2 %= (int64_t)CHIPMUNK_Q;
    int64_t x4 = x2 * x2;
    x4 %= (int64_t)CHIPMUNK_Q;
    int64_t x5 = x4 * (int64_t)x;
    return s_freduce(x5);
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
    int32_t s0 = state[0], s1 = state[1], s2 = state[2];
    int32_t m0, m1, m2;
    uint32_t i;

    for (i = 0; i < CHIPMUNK_POSEIDON_R; i++) {
        /* ---- Add round constants ---- */
        s0 = s_freduce((int64_t)s0 + s_rc[i][0]);
        s1 = s_freduce((int64_t)s1 + s_rc[i][1]);
        s2 = s_freduce((int64_t)s2 + s_rc[i][2]);

        /* ---- S-box (x^5) ---- */
        if (i < 4u || i >= 26u) {
            /* Full round: apply to all 3 words */
            s0 = s_sbox(s0);
            s1 = s_sbox(s1);
            s2 = s_sbox(s2);
        } else {
            /* Partial round: apply to first word only */
            s0 = s_sbox(s0);
        }

        /* ---- MDS matrix multiply ---- */
        int32_t tmp[3] = { s0, s1, s2 };
        s_mds_mul(tmp, state);
        s0 = state[0];
        s1 = state[1];
        s2 = state[2];
    }

    state[0] = s0;
    state[1] = s1;
    state[2] = s2;
}

int32_t chipmunk_poseidon_hash2(int32_t left, int32_t right)
{
    int32_t state[3] = { 0, left, right };  /* capacity=0, rate=inputs */
    chipmunk_poseidon_perm(state);
    return state[0];  /* squeeze capacity word */
}
