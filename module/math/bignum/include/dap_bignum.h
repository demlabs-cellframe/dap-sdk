/**
 * @file dap_bignum.h
 * @brief Multi-precision integer arithmetic.
 *
 * @authors naeper
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAP_BIGNUM_MAX_LIMBS 32

typedef struct dap_bignum {
    uint32_t limbs[DAP_BIGNUM_MAX_LIMBS];
    int      nlimbs;
    int      sign;  ///< 0 = positive, 1 = negative
} dap_bignum_t;

void dap_bignum_init(dap_bignum_t *a_bn);
void dap_bignum_set_u64(dap_bignum_t *a_bn, uint64_t a_val);
void dap_bignum_set_i32(dap_bignum_t *a_bn, int32_t a_val);

int dap_bignum_add(dap_bignum_t *a_r, const dap_bignum_t *a_a, const dap_bignum_t *a_b);
int dap_bignum_sub(dap_bignum_t *a_r, const dap_bignum_t *a_a, const dap_bignum_t *a_b);
int dap_bignum_mul(dap_bignum_t *a_r, const dap_bignum_t *a_a, const dap_bignum_t *a_b);
int dap_bignum_mod(dap_bignum_t *a_r, const dap_bignum_t *a_a, const dap_bignum_t *a_m);

int dap_bignum_mod_exp(dap_bignum_t *a_r, const dap_bignum_t *a_base,
                       const dap_bignum_t *a_exp, const dap_bignum_t *a_mod);
int dap_bignum_gcd(dap_bignum_t *a_r, const dap_bignum_t *a_a, const dap_bignum_t *a_b);
int dap_bignum_mod_inv(dap_bignum_t *a_r, const dap_bignum_t *a_a, const dap_bignum_t *a_m);

int  dap_bignum_cmp(const dap_bignum_t *a_a, const dap_bignum_t *a_b);
int  dap_bignum_is_zero(const dap_bignum_t *a_bn);
void dap_bignum_copy(dap_bignum_t *a_dst, const dap_bignum_t *a_src);

/**
 * @name Shorthand macros for bignum operations.
 *
 * Available after #include "dap_bignum.h":
 *   BN(x)               — declare dap_bignum_t x on the stack
 *   BN_SET_U64(bn, v)   — set from uint64_t
 *   BN_SET_I32(bn, v)   — set from int32_t
 *   BN_ADD(r, a, b)     — r = a + b
 *   BN_SUB(r, a, b)     — r = a - b
 *   BN_MUL(r, a, b)     — r = a * b
 *   BN_MOD(r, a, m)     — r = a mod m
 *   BN_MOD_EXP(r,b,e,m) — r = b^e mod m
 *   BN_GCD(r, a, b)     — r = gcd(a, b)
 *   BN_MOD_INV(r, a, m) — r = a^(-1) mod m
 *   BN_CMP(a, b)        — compare a vs b (negative / 0 / positive)
 *   BN_IS_ZERO(a)       — 1 if a == 0
 *   BN_COPY(dst, src)   — dst = src
 * @{
 */
#define BN(x)                  dap_bignum_t x = {0}
#define BN_SET_U64(bn, v)     dap_bignum_set_u64(&(bn), (v))
#define BN_SET_I32(bn, v)     dap_bignum_set_i32(&(bn), (v))

#define BN_ADD(r, a, b)       dap_bignum_add(&(r), &(a), &(b))
#define BN_SUB(r, a, b)       dap_bignum_sub(&(r), &(a), &(b))
#define BN_MUL(r, a, b)       dap_bignum_mul(&(r), &(a), &(b))
#define BN_MOD(r, a, m)       dap_bignum_mod(&(r), &(a), &(m))
#define BN_MOD_EXP(r, b, e, m)  dap_bignum_mod_exp(&(r), &(b), &(e), &(m))
#define BN_GCD(r, a, b)       dap_bignum_gcd(&(r), &(a), &(b))
#define BN_MOD_INV(r, a, m)   dap_bignum_mod_inv(&(r), &(a), &(m))

#define BN_CMP(a, b)          dap_bignum_cmp(&(a), &(b))
#define BN_IS_ZERO(a)         dap_bignum_is_zero(&(a))
#define BN_COPY(dst, src)     dap_bignum_copy(&(dst), &(src))
/** @} */

#ifdef __cplusplus
}
#endif
