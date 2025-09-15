/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Ltd   https://demlabs.net
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "dap_common.h"
#include "chipmunk_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file chipmunk_ring_commitment.h
 * @brief Quantum-resistant commitment system for ChipmunkRing signatures
 * 
 * This module provides commitment functionality for ChipmunkRing signatures,
 * including deterministic and random commitment generation modes for anonymity control.
 */

/**
 * @brief Create quantum-resistant commitment for ZKP (always deterministic for anonymity)
 * @param a_commitment Output commitment structure
 * @param a_public_key Public key for commitment
 * @param a_message Message for deterministic seed
 * @param a_message_size Size of message
 * @return 0 on success, negative on error
 */
int chipmunk_ring_acorn_create(chipmunk_ring_acorn_t *a_acorn,
                               const chipmunk_ring_public_key_t *a_public_key,
                               const void *a_message, size_t a_message_size,
                               size_t a_randomness_size,
                               size_t a_acorn_proof_size,
                               size_t a_linkability_tag_size);

/**
 * @brief Free memory allocated for Acorn verification dynamic arrays
 * @param a_acorn Acorn verification to free
 */
void chipmunk_ring_acorn_free(chipmunk_ring_acorn_t *a_acorn);

// NOTE: Ring-LWE layer function removed - Acorn Verification handles all needs

// NOTE: NTRU layer function removed - Acorn Verification handles all needs

// NOTE: All old quantum layer functions removed - Acorn Verification handles all needs

#ifdef __cplusplus
}
#endif
