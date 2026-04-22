/**
 * @file dap_mlkem_polyvec_{{ARCH_LOWER}}.c
 * @brief {{ARCH_NAME}} polyvec compress/decompress + basemul_acc_cached for ML-KEM.
 * @details Generated from dap_mlkem_polyvec_simd.c.tpl by dap_tpl.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#include <stdint.h>
#include <string.h>
{{ARCH_INCLUDES}}

#define MLKEM_Q     3329
#define MLKEM_QINV  ((int16_t)-3327)
#define MLKEM_N     256

{{#include IMPL_FILE}}
