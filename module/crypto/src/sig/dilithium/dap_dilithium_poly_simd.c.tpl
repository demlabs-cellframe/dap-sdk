/**
 * @file dap_dilithium_poly_{{ARCH_LOWER}}.c
 * @brief {{ARCH_NAME}} SIMD-optimized Dilithium/ML-DSA polynomial helpers.
 * @details Vectorized reduce, csubq, freeze, add, sub for int32 Q=8380417.
 *          Generated from dap_dilithium_poly_simd.c.tpl by dap_tpl.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#include <stdint.h>
{{ARCH_INCLUDES}}

{{#include PRIMITIVES_FILE}}

{{#include REDUCE_FILE}}

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
