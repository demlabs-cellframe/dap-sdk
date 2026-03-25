/**
 * @file dap_dilithium_poly_{{ARCH_LOWER}}.c
 * @brief {{ARCH_NAME}} SIMD-optimized Dilithium/ML-DSA polynomial helpers.
 * @details Vectorized reduce, csubq, freeze, add, sub, neg, shiftl,
 *          decompose, power2round, make_hint, use_hint, chknorm
 *          for int32 Q=8380417.
 *          Generated from dap_dilithium_poly_simd.c.tpl by dap_tpl.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#include <stdint.h>
{{ARCH_INCLUDES}}

{{#include PRIMITIVES_FILE}}

{{#include REDUCE_FILE}}

{{#include OPS_FILE}}

{{TARGET_ATTR}}
void dap_dilithium_poly_reduce_{{ARCH_LOWER}}(int32_t a_coeffs[DIL_N])
{
    s_poly_reduce_vec(a_coeffs);
}

{{TARGET_ATTR}}
void dap_dilithium_poly_csubq_{{ARCH_LOWER}}(int32_t a_coeffs[DIL_N])
{
    s_poly_csubq_vec(a_coeffs);
}

{{TARGET_ATTR}}
void dap_dilithium_poly_freeze_{{ARCH_LOWER}}(int32_t a_coeffs[DIL_N])
{
    s_poly_freeze_vec(a_coeffs);
}

{{TARGET_ATTR}}
void dap_dilithium_poly_add_{{ARCH_LOWER}}(int32_t * restrict a_r,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    s_poly_add_vec(a_r, a_a, a_b);
}

{{TARGET_ATTR}}
void dap_dilithium_poly_sub_{{ARCH_LOWER}}(int32_t * restrict a_r,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    s_poly_sub_vec(a_r, a_a, a_b);
}

{{TARGET_ATTR}}
void dap_dilithium_poly_neg_{{ARCH_LOWER}}(int32_t a_coeffs[DIL_N])
{
    s_poly_neg_vec(a_coeffs);
}

{{TARGET_ATTR}}
void dap_dilithium_poly_shiftl_{{ARCH_LOWER}}(int32_t a_coeffs[DIL_N],
    unsigned a_k)
{
    s_poly_shiftl_vec(a_coeffs, a_k);
}

{{TARGET_ATTR}}
void dap_dilithium_poly_decompose_{{ARCH_LOWER}}(int32_t * restrict a_a1,
    int32_t * restrict a_a0, const int32_t * restrict a_a)
{
    s_poly_decompose_vec(a_a1, a_a0, a_a);
}

{{TARGET_ATTR}}
void dap_dilithium_poly_power2round_{{ARCH_LOWER}}(int32_t * restrict a_a1,
    int32_t * restrict a_a0, const int32_t * restrict a_a)
{
    s_poly_power2round_vec(a_a1, a_a0, a_a);
}

{{TARGET_ATTR}}
unsigned dap_dilithium_poly_make_hint_{{ARCH_LOWER}}(int32_t * restrict a_h,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    return s_poly_make_hint_vec(a_h, a_a, a_b);
}

{{TARGET_ATTR}}
void dap_dilithium_poly_use_hint_{{ARCH_LOWER}}(int32_t * restrict a_r,
    const int32_t * restrict a_b, const int32_t * restrict a_h)
{
    s_poly_use_hint_vec(a_r, a_b, a_h);
}

{{TARGET_ATTR}}
int dap_dilithium_poly_chknorm_{{ARCH_LOWER}}(const int32_t *a_coeffs,
    int32_t a_bound)
{
    return s_poly_chknorm_vec(a_coeffs, a_bound);
}
