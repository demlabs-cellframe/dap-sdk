/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file ecdsa_field_{{ARCH_LOWER}}.c
 * @brief {{ARCH_NAME}} optimized secp256k1 field multiplication
 * @details Auto-generated from template.
 *
 * Key functions optimized:
 *   - field_mul: Field multiplication with interleaved reduction
 *   - field_sqr: Field squaring with interleaved reduction
 *
 * Optimizations for {{ARCH_NAME}}:
 * {{OPTIMIZATION_NOTES}}
 *
 * Performance target: {{PERF_TARGET}}
 *
 * @date 2026
 * @generated
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
{{ARCH_INCLUDES}}

#include "ecdsa_field.h"
#include "ecdsa_field_ref.h"
#include "arch/ecdsa_field_arch.h"

// ============================================================================
// {{ARCH_NAME}} Architecture-Specific Primitives
// ============================================================================

{{#include PRIMITIVES_FILE}}

// ============================================================================
// Platform-specific implementation selection
// ============================================================================

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || (defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 8)
// ============================================================================
// 64-bit: 5x52-bit limb representation
// ============================================================================

// ============================================================================
// {{ARCH_NAME}} Field Multiplication: a * b mod p
// Using interleaved multiplication and reduction (bitcoin-core style)
// ============================================================================

{{TARGET_ATTR}}
void ecdsa_field_mul_{{ARCH_LOWER}}(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b)
{
    FIELD_MUL_IMPL(r->n, a->n, b->n);
}

// ============================================================================
// {{ARCH_NAME}} Field Squaring: a^2 mod p
// Optimized for squaring - symmetric products computed once
// ============================================================================

{{TARGET_ATTR}}
void ecdsa_field_sqr_{{ARCH_LOWER}}(ecdsa_field_t *r, const ecdsa_field_t *a)
{
    FIELD_SQR_IMPL(r->n, a->n);
}

#else
// ============================================================================
// 32-bit: 10x26-bit limb representation
// For 32-bit platforms, we delegate to the reference implementation
// as the arch-specific optimizations are designed for 64-bit limbs.
// ============================================================================

void ecdsa_field_mul_{{ARCH_LOWER}}(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b)
{
    ecdsa_field_mul_ref(r, a, b);
}

void ecdsa_field_sqr_{{ARCH_LOWER}}(ecdsa_field_t *r, const ecdsa_field_t *a)
{
    ecdsa_field_sqr_ref(r, a);
}

#endif // 64-bit vs 32-bit
