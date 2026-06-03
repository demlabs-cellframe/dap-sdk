/**
 * @file dap_dilithium_sample_{{ARCH_LOWER}}.c
 * @brief {{ARCH_NAME}} SIMD-optimized Dilithium rejection sampling.
 * @details Vectorized rej_uniform (23-bit rejection from SHAKE128 output).
 *          Generated from dap_dilithium_sample_simd.c.tpl by dap_tpl.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#include <stdint.h>
{{ARCH_INCLUDES}}

{{#include SAMPLE_BODY}}

{{TARGET_ATTR}}
void dap_dilithium_rej_uniform_{{ARCH_LOWER}}(uint32_t a_coeffs[256],
                                                const uint8_t *a_buf)
{
    s_dil_rej_uniform_impl(a_coeffs, a_buf);
}
