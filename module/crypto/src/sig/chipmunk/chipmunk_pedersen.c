/*
 * chipmunk_pedersen.c — Lattice-based Pedersen commitment implementation.
 *
 * C = A * r + encode(m) mod q
 * where A ∈ R_q^{K×L}, r ∈ R_q^L short, m ∈ Z encoded as constant polynomial.
 */

#include "chipmunk_pedersen.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"
#include "chipmunk_lrs.h"
#include "chipmunk.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake256.h"
#include "dap_memwipe.h"

#include <string.h>
#include <errno.h>

#define LOG_TAG "chipmunk_pedersen"

/* Encode integer message as constant polynomial */
static void s_encode_message(chipmunk_poly_t *a_out, int64_t a_message)
{
    memset(a_out, 0, sizeof(chipmunk_poly_t));
    /* Encode message in first coefficient, reduced mod q */
    int64_t l_msg_mod = a_message % CHIPMUNK_Q;
    if (l_msg_mod < 0) l_msg_mod += CHIPMUNK_Q;
    a_out->coeffs[0] = (int32_t)l_msg_mod;
}

int chipmunk_pedersen_init(chipmunk_pedersen_params_t *a_params,
                           const uint8_t a_seed[32])
{
    if (!a_params || !a_seed) return -EINVAL;

    /* Derive matrix A from seed using SHAKE256 */
    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));
    {
        size_t l_abs_len = 32 + 18;
        uint8_t *l_abs = DAP_NEW_Z_SIZE(uint8_t, l_abs_len);
        if (!l_abs) return -ENOMEM;
        memcpy(l_abs, a_seed, 32);
        memcpy(l_abs + 32, "pedersen-matrix-v1", 18);
        dap_hash_shake256_absorb(l_state, l_abs, l_abs_len);
        DAP_DELETE(l_abs);
    }

    /* Use heap allocation. Size must be multiple of SHAKE256 rate (136 bytes)
     * to avoid overflow from squeezeblocks writing beyond buffer. */
    size_t l_needed = CHIPMUNK_N * 4;  /* 2048 bytes for 512 coefficients * 4 bytes */
    size_t l_nblocks = (l_needed + 135) / 136;  /* ceil(2048/136) = 16 blocks */
    size_t l_buf_size = l_nblocks * 136;  /* 16 * 136 = 2176 bytes */
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_buf_size);
    if (!l_buf) return -ENOMEM;

    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
            /* Squeeze random bytes for polynomial */
            dap_hash_shake256_squeezeblocks(l_buf,
                                             (l_buf_size + 135) / 136,
                                             l_state);
            /* Convert to polynomial coefficients */
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                uint32_t l_val;
                memcpy(&l_val, &l_buf[k * 4], 4);
                a_params->A[i][j].coeffs[k] = (int32_t)(l_val % CHIPMUNK_Q);
            }
        }
    }

    DAP_DELETE(l_buf);
    a_params->initialized = true;
    return 0;
}

int chipmunk_pedersen_commit(chipmunk_pedersen_commit_t *a_commit,
                             const chipmunk_pedersen_params_t *a_params,
                             int64_t a_message,
                             const uint8_t a_randomness_seed[32])
{
    if (!a_commit || !a_params || !a_randomness_seed) return -EINVAL;
    if (!a_params->initialized) return -EINVAL;

    /* Generate random blinding vector r from seed */
    chipmunk_poly_t *l_r = DAP_NEW_Z_COUNT(chipmunk_poly_t, CHIPMUNK_LRS_K);
    if (!l_r) return -ENOMEM;
    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));
    {
        size_t l_abs_len = 32 + 22;
        uint8_t *l_abs = DAP_NEW_Z_SIZE(uint8_t, l_abs_len);
        if (!l_abs) { DAP_DELETE(l_r); return -ENOMEM; }
        memcpy(l_abs, a_randomness_seed, 32);
        memcpy(l_abs + 32, "pedersen-randomness-v1", 22);
        dap_hash_shake256_absorb(l_state, l_abs, l_abs_len);
        DAP_DELETE(l_abs);
    }

    /* Heap allocation — must be multiple of SHAKE256 rate (136 bytes) */
    size_t l_needed = CHIPMUNK_N * 4;
    size_t l_nblocks = (l_needed + 135) / 136;
    size_t l_buf_size = l_nblocks * 136;
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_buf_size);
    if (!l_buf) { DAP_DELETE(l_r); return -ENOMEM; }

    for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
        dap_hash_shake256_squeezeblocks(l_buf,
                                         (l_buf_size + 135) / 136,
                                         l_state);
        for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
            uint32_t l_val;
            memcpy(&l_val, &l_buf[k * 4], 4);
            /* Short randomness: coefficients in [-eta, eta] */
            l_r[j].coeffs[k] = (int32_t)((l_val % (2 * 13 + 1)) - 13);
        }
    }
    DAP_DELETE(l_buf);

    /* Encode message */
    chipmunk_poly_t l_m;
    s_encode_message(&l_m, a_message);

    /* Compute C = A * r + encode(m) */
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        /* C[i] = Σ_j A[i][j] * r[j] + m (for i=0, else just A*r) */
        chipmunk_poly_t l_sum;
        memset(&l_sum, 0, sizeof(l_sum));

        for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
            /* A[i][j] * r[j] in NTT domain */
            chipmunk_poly_t l_a_ntt = a_params->A[i][j];
            chipmunk_poly_t l_r_ntt = l_r[j];
            chipmunk_ntt(l_a_ntt.coeffs);
            chipmunk_ntt(l_r_ntt.coeffs);

            chipmunk_poly_t l_prod;
            chipmunk_poly_mul_ntt(&l_prod, &l_a_ntt, &l_r_ntt);
            chipmunk_invntt(l_prod.coeffs);

            /* Accumulate */
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                l_sum.coeffs[k] = (int32_t)(((int64_t)l_sum.coeffs[k] + l_prod.coeffs[k])
                                             % CHIPMUNK_Q);
                if (l_sum.coeffs[k] < 0) l_sum.coeffs[k] += CHIPMUNK_Q;
            }
        }

        /* Add encoded message to first component */
        if (i == 0) {
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                l_sum.coeffs[k] = (int32_t)(((int64_t)l_sum.coeffs[k] + l_m.coeffs[k])
                                             % CHIPMUNK_Q);
                if (l_sum.coeffs[k] < 0) l_sum.coeffs[k] += CHIPMUNK_Q;
            }
        }

        a_commit->C[i] = l_sum;
    }

    /* Wipe secret randomness */
    dap_memwipe(l_r, CHIPMUNK_LRS_K * sizeof(chipmunk_poly_t));
    DAP_DELETE(l_r);
    return 0;
}

int chipmunk_pedersen_commit_explicit(chipmunk_pedersen_commit_t *a_commit,
                                       const chipmunk_pedersen_params_t *a_params,
                                       int64_t a_message,
                                       const chipmunk_poly_t a_randomness[CHIPMUNK_LRS_K])
{
    if (!a_commit || !a_params || !a_randomness) return -EINVAL;
    if (!a_params->initialized) return -EINVAL;

    chipmunk_poly_t l_m;
    s_encode_message(&l_m, a_message);

    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        chipmunk_poly_t l_sum;
        memset(&l_sum, 0, sizeof(l_sum));

        for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
            chipmunk_poly_t l_a_ntt = a_params->A[i][j];
            chipmunk_poly_t l_r_ntt = a_randomness[j];
            chipmunk_ntt(l_a_ntt.coeffs);
            chipmunk_ntt(l_r_ntt.coeffs);

            chipmunk_poly_t l_prod;
            chipmunk_poly_mul_ntt(&l_prod, &l_a_ntt, &l_r_ntt);
            chipmunk_invntt(l_prod.coeffs);

            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                l_sum.coeffs[k] = (int32_t)(((int64_t)l_sum.coeffs[k] + l_prod.coeffs[k])
                                             % CHIPMUNK_Q);
                if (l_sum.coeffs[k] < 0) l_sum.coeffs[k] += CHIPMUNK_Q;
            }
        }

        if (i == 0) {
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                l_sum.coeffs[k] = (int32_t)(((int64_t)l_sum.coeffs[k] + l_m.coeffs[k])
                                             % CHIPMUNK_Q);
                if (l_sum.coeffs[k] < 0) l_sum.coeffs[k] += CHIPMUNK_Q;
            }
        }

        a_commit->C[i] = l_sum;
    }
    return 0;
}

int chipmunk_pedersen_verify_opening(const chipmunk_pedersen_commit_t *a_commit,
                                     const chipmunk_pedersen_params_t *a_params,
                                     const chipmunk_pedersen_opening_t *a_opening)
{
    if (!a_commit || !a_params || !a_opening) return -EINVAL;
    if (!a_params->initialized) return -EINVAL;

    /* Recompute C' = A * r + encode(m) */
    chipmunk_pedersen_commit_t l_recomputed;
    chipmunk_poly_t l_m;
    s_encode_message(&l_m, a_opening->message);

    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        chipmunk_poly_t l_sum;
        memset(&l_sum, 0, sizeof(l_sum));

        for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
            chipmunk_poly_t l_a_ntt = a_params->A[i][j];
            chipmunk_poly_t l_r_ntt = a_opening->randomness[j];
            chipmunk_ntt(l_a_ntt.coeffs);
            chipmunk_ntt(l_r_ntt.coeffs);

            chipmunk_poly_t l_prod;
            chipmunk_poly_mul_ntt(&l_prod, &l_a_ntt, &l_r_ntt);
            chipmunk_invntt(l_prod.coeffs);

            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                l_sum.coeffs[k] = (int32_t)(((int64_t)l_sum.coeffs[k] + l_prod.coeffs[k])
                                             % CHIPMUNK_Q);
                if (l_sum.coeffs[k] < 0) l_sum.coeffs[k] += CHIPMUNK_Q;
            }
        }

        if (i == 0) {
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                l_sum.coeffs[k] = (int32_t)(((int64_t)l_sum.coeffs[k] + l_m.coeffs[k])
                                             % CHIPMUNK_Q);
                if (l_sum.coeffs[k] < 0) l_sum.coeffs[k] += CHIPMUNK_Q;
            }
        }

        l_recomputed.C[i] = l_sum;
    }

    /* Compare C' == C */
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
            if (a_commit->C[i].coeffs[k] != l_recomputed.C[i].coeffs[k]) {
                return 0; /* Invalid */
            }
        }
    }

    return 1; /* Valid */
}

void chipmunk_pedersen_add(chipmunk_pedersen_commit_t *a_sum,
                           const chipmunk_pedersen_commit_t *a_c1,
                           const chipmunk_pedersen_commit_t *a_c2)
{
    if (!a_sum || !a_c1 || !a_c2) return;

    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
            a_sum->C[i].coeffs[k] = (int32_t)(((int64_t)a_c1->C[i].coeffs[k]
                                                + a_c2->C[i].coeffs[k])
                                               % CHIPMUNK_Q);
            if (a_sum->C[i].coeffs[k] < 0) a_sum->C[i].coeffs[k] += CHIPMUNK_Q;
        }
    }
}

int chipmunk_pedersen_commit_serialize(uint8_t *a_out, size_t a_out_size,
                                       const chipmunk_pedersen_commit_t *a_commit)
{
    if (!a_out || !a_commit) return -EINVAL;
    /* Each polynomial: CHIPMUNK_N * 4 bytes = 2048 bytes */
    size_t l_needed = (size_t)CHIPMUNK_PEDERSEN_K * CHIPMUNK_N * sizeof(int32_t);
    if (a_out_size < l_needed) return -ENOMEM;

    size_t l_off = 0;
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        memcpy(a_out + l_off, a_commit->C[i].coeffs, CHIPMUNK_N * sizeof(int32_t));
        l_off += CHIPMUNK_N * sizeof(int32_t);
    }
    return 0;
}

int chipmunk_pedersen_commit_deserialize(chipmunk_pedersen_commit_t *a_commit,
                                         const uint8_t *a_in, size_t a_in_size)
{
    if (!a_commit || !a_in) return -EINVAL;
    size_t l_needed = (size_t)CHIPMUNK_PEDERSEN_K * CHIPMUNK_N * sizeof(int32_t);
    if (a_in_size < l_needed) return -ENOMEM;

    size_t l_off = 0;
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        memcpy(a_commit->C[i].coeffs, a_in + l_off, CHIPMUNK_N * sizeof(int32_t));
        l_off += CHIPMUNK_N * sizeof(int32_t);
    }
    for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K; ++i) {
        for (uint32_t j = 0; j < CHIPMUNK_N; ++j) {
            if (a_commit->C[i].coeffs[j] < 0 || a_commit->C[i].coeffs[j] >= CHIPMUNK_Q) {
                return -EINVAL;
            }
        }
    }
    return 0;
}
