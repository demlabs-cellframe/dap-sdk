/*
 * chipmunk_rs.c — Reed-Solomon encoding via 2048-point coset NTT.
 *
 * See chipmunk_rs.h for documentation.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "chipmunk_rs.h"
#include "chipmunk_field.h"
#include <string.h>

#include "dap_common.h"

#define LOG_TAG "chipmunk_rs"

/* Field multiplication in [0, q). */
static inline int32_t s_fqmul(int32_t a_a, int32_t a_b)
{
    int64_t l_t = (int64_t)a_a * (int64_t)a_b;
    int32_t l_r = (int32_t)(l_t % (int64_t)CHIPMUNK_Q);
    if (l_r < 0) l_r += (int32_t)CHIPMUNK_Q;
    return l_r;
}

int chipmunk_rs_encode(int32_t codeword[CHIPMUNK_RS_CODE_LEN],
                        const int32_t poly[CHIPMUNK_RS_MSG_LEN])
{
    if (!codeword || !poly)
        return -1;

    /*
     * Pad poly[0..511] to 2048 coefficients: copy 512, zero the rest.
     * If codeword != poly, this is a straightforward memcpy + memset.
     * If they alias (same pointer), we rely on the NTT touching only
     * elements [0..2047] which includes [512..2047] (already zero).
     */
    if (codeword != poly) {
        memcpy(codeword, poly, (size_t)CHIPMUNK_RS_MSG_LEN * sizeof(int32_t));
        memset(codeword + CHIPMUNK_RS_MSG_LEN, 0,
               (size_t)(CHIPMUNK_RS_CODE_LEN - CHIPMUNK_RS_MSG_LEN) * sizeof(int32_t));
    } else {
        memset(codeword + CHIPMUNK_RS_MSG_LEN, 0,
               (size_t)(CHIPMUNK_RS_CODE_LEN - CHIPMUNK_RS_MSG_LEN) * sizeof(int32_t));
    }

    /* Coset NTT: multiply coeff[i] by g^i, then forward NTT.
     * chipmunk_fri_ntt_coset_forward handles the shift internally.
     * Result: codeword[k] = f(g * omega^k) in natural order. */
    chipmunk_fri_ntt_coset_forward(codeword, CHIPMUNK_RS_COSET_G);

    return 0;
}

int chipmunk_rs_interpolate(int32_t poly[CHIPMUNK_RS_MSG_LEN],
                              const int32_t codeword[CHIPMUNK_RS_CODE_LEN])
{
    if (!poly || !codeword)
        return -1;

    /*
     * Inverse coset NTT:
     *   1. Copy codeword to a working buffer (2048 elements).
     *   2. Inverse NTT → coeff[i] * g^i for i = 0..2047.
     *   3. Multiply by g^{-i} to undo the coset shift.
     *   4. Extract first 512 coefficients.
     */
    int32_t l_work[CHIPMUNK_RS_CODE_LEN];
    memcpy(l_work, codeword, sizeof(l_work));

    chipmunk_fri_ntt_inverse(l_work);

    /* Undo coset shift: coeff[i] = work[i] * g^{-i} */
    int32_t l_g_inv = chipmunk_field_inv((int32_t)CHIPMUNK_RS_COSET_G);
    int32_t l_g_pow_inv = 1;  /* g^0 = 1 */
    for (unsigned int i = 0; i < CHIPMUNK_RS_MSG_LEN; ++i) {
        poly[i] = s_fqmul(l_work[i], l_g_pow_inv);
        l_g_pow_inv = s_fqmul(l_g_pow_inv, l_g_inv);
    }

    return 0;
}

int32_t chipmunk_rs_eval(const int32_t *poly, uint32_t n, int32_t x)
{
    if (!poly || n == 0)
        return 0;

    /* Horner's method: f(x) = (...((poly[n-1]*x + poly[n-2])*x + ...)*x + poly[0]) */
    int32_t l_result = 0;
    for (uint32_t i = n; i > 0; --i) {
        l_result = s_fqmul(l_result, x) + poly[i - 1];
        if (l_result >= (int32_t)CHIPMUNK_Q) {
            l_result -= (int32_t)CHIPMUNK_Q;
        } else if (l_result < 0) {
            l_result += (int32_t)CHIPMUNK_Q;
        }
    }

    /* Final reduction */
    if (l_result >= (int32_t)CHIPMUNK_Q)
        l_result -= (int32_t)CHIPMUNK_Q;
    else if (l_result < 0)
        l_result += (int32_t)CHIPMUNK_Q;

    return l_result;
}
