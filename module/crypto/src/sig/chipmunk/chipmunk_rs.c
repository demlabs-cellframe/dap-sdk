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

/* Parameterized field multiplication (Phase 9.13h). */
static inline int32_t s_fqmul_q(int32_t a_a, int32_t a_b, uint64_t q)
{
    int64_t l_t = (int64_t)a_a * (int64_t)a_b;
    int32_t l_r = (int32_t)(l_t % (int64_t)q);
    if (l_r < 0) l_r += (int32_t)q;
    return l_r;
}

int chipmunk_rs_encode(int32_t codeword[CHIPMUNK_RS_CODE_LEN],
                        const int32_t poly[CHIPMUNK_RS_MSG_LEN])
{
    if (!codeword || !poly)
        return -1;

    if (codeword != poly) {
        memcpy(codeword, poly, (size_t)CHIPMUNK_RS_MSG_LEN * sizeof(int32_t));
        memset(codeword + CHIPMUNK_RS_MSG_LEN, 0,
               (size_t)(CHIPMUNK_RS_CODE_LEN - CHIPMUNK_RS_MSG_LEN) * sizeof(int32_t));
    } else {
        memset(codeword + CHIPMUNK_RS_MSG_LEN, 0,
               (size_t)(CHIPMUNK_RS_CODE_LEN - CHIPMUNK_RS_MSG_LEN) * sizeof(int32_t));
    }

    chipmunk_fri_ntt_coset_forward(codeword, CHIPMUNK_RS_COSET_G);

    return 0;
}

int chipmunk_rs_encode_q(int32_t codeword[CHIPMUNK_RS_CODE_LEN],
                          const int32_t poly[CHIPMUNK_RS_MSG_LEN],
                          const chipmunk_fri_ntt_ctx_t *ntt_ctx)
{
    if (!codeword || !poly || !ntt_ctx)
        return -1;

    if (codeword != poly) {
        memcpy(codeword, poly, (size_t)CHIPMUNK_RS_MSG_LEN * sizeof(int32_t));
        memset(codeword + CHIPMUNK_RS_MSG_LEN, 0,
               (size_t)(CHIPMUNK_RS_CODE_LEN - CHIPMUNK_RS_MSG_LEN) * sizeof(int32_t));
    } else {
        memset(codeword + CHIPMUNK_RS_MSG_LEN, 0,
               (size_t)(CHIPMUNK_RS_CODE_LEN - CHIPMUNK_RS_MSG_LEN) * sizeof(int32_t));
    }

    chipmunk_fri_ntt_coset_forward_q(codeword, CHIPMUNK_RS_COSET_G, ntt_ctx);

    return 0;
}

int chipmunk_rs_interpolate(int32_t poly[CHIPMUNK_RS_MSG_LEN],
                              const int32_t codeword[CHIPMUNK_RS_CODE_LEN])
{
    return chipmunk_rs_interpolate_q(poly, codeword, (uint64_t)CHIPMUNK_Q, NULL);
}

int chipmunk_rs_interpolate_q(int32_t poly[CHIPMUNK_RS_MSG_LEN],
                                const int32_t codeword[CHIPMUNK_RS_CODE_LEN],
                                uint64_t q,
                                const chipmunk_fri_ntt_ctx_t *ntt_ctx)
{
    if (!poly || !codeword)
        return -1;

    int32_t l_work[CHIPMUNK_RS_CODE_LEN];
    memcpy(l_work, codeword, sizeof(l_work));

    if (ntt_ctx) {
        chipmunk_fri_ntt_inverse_q(l_work, ntt_ctx);
    } else {
        /* Fallback to global NTT (only valid for q == CHIPMUNK_Q). */
        chipmunk_fri_ntt_inverse(l_work);
    }

    /* Undo coset shift: coeff[i] = work[i] * g^{-i} */
    int32_t l_g_inv = chipmunk_field_inv_q((int32_t)CHIPMUNK_RS_COSET_G, q);
    int32_t l_g_pow_inv = 1;
    for (unsigned int i = 0; i < CHIPMUNK_RS_MSG_LEN; ++i) {
        poly[i] = s_fqmul_q(l_work[i], l_g_pow_inv, q);
        l_g_pow_inv = s_fqmul_q(l_g_pow_inv, l_g_inv, q);
    }

    return 0;
}

int32_t chipmunk_rs_eval(const int32_t *poly, uint32_t n, int32_t x)
{
    return chipmunk_rs_eval_q(poly, n, x, (uint64_t)CHIPMUNK_Q);
}

int32_t chipmunk_rs_eval_q(const int32_t *poly, uint32_t n, int32_t x, uint64_t q)
{
    if (!poly || n == 0)
        return 0;

    int32_t l_result = 0;
    for (uint32_t i = n; i > 0; --i) {
        l_result = s_fqmul_q(l_result, x, q) + poly[i - 1];
        if (l_result >= (int32_t)q) {
            l_result -= (int32_t)q;
        } else if (l_result < 0) {
            l_result += (int32_t)q;
        }
    }

    if (l_result >= (int32_t)q)
        l_result -= (int32_t)q;
    else if (l_result < 0)
        l_result += (int32_t)q;

    return l_result;
}
