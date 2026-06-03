/**
 * @file dap_mlkem_rej_uniform_{{ARCH_LOWER}}.c
 * @brief {{ARCH_NAME}} rejection sampling for ML-KEM gen_matrix.
 * @details Vectorized rej_uniform (12-bit rejection from SHAKE128 output).
 *          Generated from dap_mlkem_rej_uniform_simd.c.tpl by dap_tpl.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#include <stdint.h>
{{ARCH_INCLUDES}}

#define MLKEM_Q 3329

{{#include SAMPLE_BODY}}

{{TARGET_ATTR}}
unsigned dap_mlkem_rej_uniform_{{ARCH_LOWER}}(int16_t *a_r, unsigned a_len,
                                               const uint8_t *a_buf, unsigned a_buflen)
{
    return s_rej_uniform_impl(a_r, a_len, a_buf, a_buflen);
}
