/**
 * @file dap_poly1305_{{ARCH_LOWER}}.c
 * @brief Poly1305 {{ARCH_NAME}} multi-block (4-block parallel).
 * @details Generated from dap_poly1305_simd.c.tpl by dap_tpl.
 *          State conversion 44-44-42 → radix-2^26 for SIMD,
 *          Horner combine back to 44-44-42 with donna scalar.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * @generated
 */

#include <stdint.h>
#include <string.h>
{{ARCH_INCLUDES}}
#include "dap_poly1305_internal.h"

#if defined(__SIZEOF_INT128__) && (defined(__x86_64__) || defined(__aarch64__))

{{#include IMPL_FILE}}

#endif
