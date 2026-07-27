/*
 * chipmunk_fri_ntt.h — 2048-point Number Theoretic Transform for FRI-DEEP.
 *
 * Per-q parameterized NTT.  All callers build a chipmunk_fri_ntt_ctx_t
 * via chipmunk_fri_ntt_ctx_init() and pass it to the _q variants.
 *
 * Domain: {omega^0, omega^1, ..., omega^{2047}}  (subgroup of F_q*)
 * Coset:  {g * omega^0, g * omega^1, ..., g * omega^{2047}}
 *
 * Non-Montgomery: all values in [0, q).  Direct modular arithmetic
 * matches the FRI folding/evaluation code path — no domain conversions.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once
#ifndef _CHIPMUNK_FRI_NTT_H_
#define _CHIPMUNK_FRI_NTT_H_

#include <stdint.h>
#include "chipmunk_field.h"

#ifdef __cplusplus
extern "C" {
#endif

/* NTT size: 2048 = 2^11 (FRI evaluation domain, blowup factor 4 × N=512) */
#define CHIPMUNK_FRI_NTT_LOG  11u
#define CHIPMUNK_FRI_NTT_SIZE (1u << CHIPMUNK_FRI_NTT_LOG)  /* 2048 */

/* -------------------------------------------------------------------------
 * Per-q NTT context
 *
 * Holds twiddle tables for an arbitrary prime q. Built by
 * chipmunk_fri_ntt_ctx_init, freed by chipmunk_fri_ntt_ctx_free.
 * ---------------------------------------------------------------------- */

typedef struct chipmunk_fri_ntt_ctx {
    uint64_t  q;           /* field modulus */
    int32_t   omega;       /* primitive CHIPMUNK_FRI_NTT_SIZE-th root */
    int32_t   omega_inv;   /* its inverse */
    int32_t   inv_n;       /* N^{-1} mod q (for inverse NTT scaling) */
    int32_t  *zetas;       /* omega^k for k = 0..N-1 (heap) */
    int32_t  *zetas_inv;   /* omega^{-k} for k = 0..N-1 (heap) */
} chipmunk_fri_ntt_ctx_t;

/**
 * @brief Build per-q NTT twiddle tables.
 * @param ctx         Output context (caller allocates struct, tables heap-alloc'd).
 * @param q           Prime modulus.
 * @param two_adicity Must equal CHIPMUNK_FRI_NTT_LOG (11).
 * @return 0 on success, negative on error.
 */
int chipmunk_fri_ntt_ctx_init(chipmunk_fri_ntt_ctx_t *ctx, uint64_t q,
                                uint32_t two_adicity);

/** @brief Free heap resources in a per-q NTT context. */
void chipmunk_fri_ntt_ctx_free(chipmunk_fri_ntt_ctx_t *ctx);

/** @brief Per-q forward NTT using ctx->zetas. */
void chipmunk_fri_ntt_forward_q(int32_t a[CHIPMUNK_FRI_NTT_SIZE],
                                  const chipmunk_fri_ntt_ctx_t *ctx);

/** @brief Per-q inverse NTT using ctx->zetas_inv and ctx->inv_n. */
void chipmunk_fri_ntt_inverse_q(int32_t a[CHIPMUNK_FRI_NTT_SIZE],
                                  const chipmunk_fri_ntt_ctx_t *ctx);

/** @brief Per-q coset forward NTT. */
void chipmunk_fri_ntt_coset_forward_q(int32_t a[CHIPMUNK_FRI_NTT_SIZE],
                                        int32_t coset_g,
                                        const chipmunk_fri_ntt_ctx_t *ctx);

/** @brief Fill domain {omega^0, omega^1, ..., omega^{N-1}} using ctx->omega. */
void chipmunk_fri_ntt_domain_q(int32_t domain[CHIPMUNK_FRI_NTT_SIZE],
                                const chipmunk_fri_ntt_ctx_t *ctx);

/** @brief Fill coset domain {g*omega^0, g*omega^1, ..., g*omega^{N-1}}. */
void chipmunk_fri_ntt_coset_domain_q(int32_t domain[CHIPMUNK_FRI_NTT_SIZE],
                                       int32_t coset_g,
                                       const chipmunk_fri_ntt_ctx_t *ctx);

/**
 * @brief Bit-reverse an 11-bit index.
 *
 * @param x Index in [0, 2048).
 * @return   Bit-reversed index.
 */
static inline uint32_t chipmunk_fri_ntt_brv11(uint32_t a_x)
{
    uint32_t l_r = 0;
    for (unsigned i = 0; i < CHIPMUNK_FRI_NTT_LOG; ++i) {
        l_r = (l_r << 1) | (a_x & 1u);
        a_x >>= 1;
    }
    return l_r;
}

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_FRI_NTT_H_ */
