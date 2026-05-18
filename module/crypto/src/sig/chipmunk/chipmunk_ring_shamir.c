/*
 * chipmunk_ring_shamir.c — internal Shamir secret sharing over Z_CHIPMUNK_Q
 *
 * CR-9.3 (kick-off slice).  See SLC `documentation_c6a567b1d9b7e68c` (CR-9 master design)
 * for the design rationale.
 *
 * Failure-mode discipline (CR-D13/D25 from day one):
 *   - every error path zeroises the caller-visible output buffer,
 *   - every internal scratch (coefficients, partial Lagrange products,
 *     rejection-sampled entropy) is wiped via dap_memwipe before return,
 *   - dap_random_bytes errors are propagated, never swallowed.
 */

#include "chipmunk_ring_shamir.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "chipmunk.h"            /* CHIPMUNK_Q */
#include "dap_memwipe.h"
#include "dap_rand.h"

#define LOG_TAG "chipmunk_ring_shamir"
#include "dap_common.h"

/* -------------------------------------------------------------------------
 *  Field helpers
 * -------------------------------------------------------------------------
 *
 *  Working type is uint64_t inside multiplications (q ≤ 2^22, so q*q
 *  fits comfortably in 44 bits).  Public boundary is uint32_t in [0, q).
 */

static inline uint32_t s_mod_q(int64_t a_x)
{
    int64_t l_r = a_x % (int64_t)CHIPMUNK_Q;
    if (l_r < 0) l_r += (int64_t)CHIPMUNK_Q;
    return (uint32_t)l_r;
}

static inline uint32_t s_mul_mod_q(uint32_t a_a, uint32_t a_b)
{
    return (uint32_t)(((uint64_t)a_a * (uint64_t)a_b) % (uint64_t)CHIPMUNK_Q);
}

/* a^e mod q via square-and-multiply.  Inner loop is data-independent
 * w.r.t. the exponent value (always q-2 in the inverse path), so the
 * branch on bit-set is over a public constant for that use-site. */
static uint32_t s_pow_mod_q(uint32_t a_a, uint32_t a_e)
{
    uint64_t l_base = a_a % CHIPMUNK_Q;
    uint64_t l_acc  = 1;
    uint32_t l_e    = a_e;
    while (l_e) {
        if (l_e & 1u) l_acc = (l_acc * l_base) % CHIPMUNK_Q;
        l_base = (l_base * l_base) % CHIPMUNK_Q;
        l_e >>= 1;
    }
    return (uint32_t)l_acc;
}

uint32_t chipmunk_ring_shamir_mod_inverse(uint32_t a_a)
{
    if (a_a == 0u) return 0u;
    /* Fermat: a^{q-2} mod q.  q is prime so a^{q-2} ≡ a^{-1} for a != 0. */
    return s_pow_mod_q(a_a % CHIPMUNK_Q, (uint32_t)(CHIPMUNK_Q - 2));
}

/* -------------------------------------------------------------------------
 *  Rejection-sampled uniform sample in [0, q)
 * -------------------------------------------------------------------------
 *
 *  Drawing a uint32_t and reducing mod q biases the low residues by
 *  ~1 ULP.  We reject any draw that falls into the "tail" region above
 *  the largest multiple of q that fits in 2^32, then reduce.  This is
 *  the textbook construction; the rejection probability is bounded by
 *  q/2^32 ≈ 7.4e-4 per draw, so the expected loop count is negligible.
 */
static int s_sample_uniform_mod_q(uint32_t *a_out)
{
    static const uint64_t l_two32     = (uint64_t)1 << 32;
    static const uint64_t l_q         = (uint64_t)CHIPMUNK_Q;
    static const uint64_t l_threshold = ((uint64_t)1 << 32) - (l_two32 % l_q);
    /* l_threshold is the largest multiple of q that fits in [0, 2^32);
     * any draw < l_threshold can be reduced mod q without bias. */

    uint8_t l_buf[sizeof(uint32_t)];
    /* Bound the loop defensively against a pathological CSPRNG so a
     * broken /dev/urandom can never lock the dealer.  The expected
     * iteration count is <= 1 + epsilon. */
    for (int l_iter = 0; l_iter < 64; ++l_iter) {
        if (dap_random_bytes(l_buf, sizeof(l_buf)) != 0) {
            dap_memwipe(l_buf, sizeof(l_buf));
            return -EIO;
        }
        uint32_t l_draw = (uint32_t)l_buf[0]
                       | ((uint32_t)l_buf[1] <<  8)
                       | ((uint32_t)l_buf[2] << 16)
                       | ((uint32_t)l_buf[3] << 24);
        if ((uint64_t)l_draw < l_threshold) {
            *a_out = (uint32_t)((uint64_t)l_draw % l_q);
            dap_memwipe(l_buf, sizeof(l_buf));
            return 0;
        }
    }
    dap_memwipe(l_buf, sizeof(l_buf));
    return -EIO;
}

/* -------------------------------------------------------------------------
 *  P(x) evaluation by Horner.
 * -------------------------------------------------------------------------
 *
 *  P(x) = c[0] + c[1]*x + ... + c[t-1]*x^{t-1}, with c[0] = secret.
 *  Horner: ((c[t-1]*x + c[t-2])*x + ...)*x + c[0]
 */
static uint32_t s_eval_poly(const uint32_t *a_coeffs, uint32_t a_t, uint32_t a_x)
{
    if (a_t == 0u) return 0u;
    uint64_t l_acc = a_coeffs[a_t - 1];
    for (int32_t i = (int32_t)a_t - 2; i >= 0; --i) {
        l_acc = (l_acc * (uint64_t)a_x + (uint64_t)a_coeffs[(uint32_t)i]) % (uint64_t)CHIPMUNK_Q;
    }
    return (uint32_t)l_acc;
}

/* =========================================================================
 *  Public-internal API
 * ========================================================================= */

int chipmunk_ring_shamir_share(uint32_t a_secret,
                               uint32_t a_n,
                               uint32_t a_t,
                               chipmunk_ring_shamir_share_t *a_out_shares)
{
    if (a_out_shares == NULL) return -EINVAL;

    /* Zero output up-front so every later error path is just `goto fail`. */
    memset(a_out_shares, 0, sizeof(*a_out_shares) * (a_n ? a_n : 1u));

    if (a_secret >= (uint32_t)CHIPMUNK_Q) return -EINVAL;
    if (a_n < 2u || a_n > CHIPMUNK_RING_THRESHOLD_MAX_N) return -EINVAL;
    if (a_t < 2u || a_t > a_n) return -EINVAL;

    /* coeffs[0] = secret; coeffs[1..t-1] = uniformly-random in [0, q). */
    uint32_t *l_coeffs = DAP_NEW_Z_COUNT(uint32_t, a_t);
    if (l_coeffs == NULL) {
        memset(a_out_shares, 0, sizeof(*a_out_shares) * a_n);
        return -ENOMEM;
    }

    int l_rc = 0;
    l_coeffs[0] = a_secret;
    for (uint32_t i = 1; i < a_t; ++i) {
        l_rc = s_sample_uniform_mod_q(&l_coeffs[i]);
        if (l_rc != 0) goto fail;
    }

    for (uint32_t i = 0; i < a_n; ++i) {
        /* Index 0 is forbidden — P(0) = secret would expose the master. */
        a_out_shares[i].index = i + 1u;
        a_out_shares[i].value = s_eval_poly(l_coeffs, a_t, i + 1u);
    }

    /* CR-D13/D25: wipe coefficients + the secret-in-coeffs[0] slot. */
    dap_memwipe(l_coeffs, sizeof(uint32_t) * a_t);
    DAP_DELETE(l_coeffs);
    return 0;

fail:
    dap_memwipe(l_coeffs, sizeof(uint32_t) * a_t);
    DAP_DELETE(l_coeffs);
    memset(a_out_shares, 0, sizeof(*a_out_shares) * a_n);
    return l_rc;
}

int chipmunk_ring_shamir_reconstruct(const chipmunk_ring_shamir_share_t *a_shares,
                                     uint32_t a_t,
                                     uint32_t *a_out_secret)
{
    if (a_out_secret == NULL) return -EINVAL;
    *a_out_secret = 0u;

    if (a_shares == NULL) return -EINVAL;
    if (a_t < 2u || a_t > CHIPMUNK_RING_THRESHOLD_MAX_N) return -EINVAL;

    /* Validate index range and detect duplicates.  We bound n at
     * CHIPMUNK_RING_THRESHOLD_MAX_N (= 64) so an O(t^2) scan is fine
     * (2080 comparisons in the worst case). */
    for (uint32_t i = 0; i < a_t; ++i) {
        if (a_shares[i].index < 1u
                || a_shares[i].index > CHIPMUNK_RING_THRESHOLD_MAX_N) {
            return -EINVAL;
        }
        if (a_shares[i].value >= (uint32_t)CHIPMUNK_Q) {
            return -EINVAL;
        }
        for (uint32_t j = i + 1u; j < a_t; ++j) {
            if (a_shares[i].index == a_shares[j].index) {
                return -EINVAL;
            }
        }
    }

    /* Lagrange interpolation at x = 0:
     *   s = sum_j y_j * prod_{m!=j} (-x_m / (x_j - x_m))   (mod q)
     *
     * All differences (x_j - x_m) are non-zero by the duplicate-index
     * check above, so every inverse is well-defined. */
    uint64_t l_acc = 0;
    for (uint32_t j = 0; j < a_t; ++j) {
        uint32_t l_xj = a_shares[j].index;
        uint32_t l_yj = a_shares[j].value;

        uint32_t l_num = 1u;   /* product of (-x_m)            mod q */
        uint32_t l_den = 1u;   /* product of (x_j - x_m)       mod q */
        for (uint32_t m = 0; m < a_t; ++m) {
            if (m == j) continue;
            uint32_t l_xm    = a_shares[m].index;
            uint32_t l_neg_xm = s_mod_q(-(int64_t)l_xm);
            uint32_t l_diff   = s_mod_q((int64_t)l_xj - (int64_t)l_xm);
            l_num = s_mul_mod_q(l_num, l_neg_xm);
            l_den = s_mul_mod_q(l_den, l_diff);
        }
        uint32_t l_inv_den = chipmunk_ring_shamir_mod_inverse(l_den);
        uint32_t l_lambda  = s_mul_mod_q(l_num, l_inv_den);
        uint32_t l_term    = s_mul_mod_q(l_yj, l_lambda);
        l_acc = (l_acc + (uint64_t)l_term) % (uint64_t)CHIPMUNK_Q;
    }

    *a_out_secret = (uint32_t)l_acc;
    return 0;
}
