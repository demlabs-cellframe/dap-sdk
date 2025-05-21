/*
 * Authors:
 * Dmitriy A. Gearasimov <kahovski@gmail.com>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2024
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
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

#ifndef _DAP_CHIPMUNK_INTERNAL_H_
#define _DAP_CHIPMUNK_INTERNAL_H_

#include "chipmunk.h"

// Internal polynomial operations
void s_chipmunk_poly_ntt(chipmunk_poly_t *a_poly);
void s_chipmunk_poly_invntt(chipmunk_poly_t *a_poly);
void s_chipmunk_poly_add(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_a, const chipmunk_poly_t *a_b);
void s_chipmunk_poly_sub(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_a, const chipmunk_poly_t *a_b);
void s_chipmunk_poly_pointwise(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_a, const chipmunk_poly_t *a_b);
void s_chipmunk_poly_uniform(chipmunk_poly_t *a_poly, const uint8_t a_seed[32], uint16_t a_nonce);
void s_chipmunk_poly_challenge(chipmunk_poly_t *a_poly, const uint8_t a_seed[32]);
int s_chipmunk_poly_chknorm(const chipmunk_poly_t *a_poly, int32_t a_bound);

// Internal hint functions
void s_chipmunk_make_hint(uint8_t a_hint[CHIPMUNK_N/8], const chipmunk_poly_t *a_z, const chipmunk_poly_t *a_r);
void s_chipmunk_use_hint(chipmunk_poly_t *a_out, const chipmunk_poly_t *a_a, const uint8_t a_hint[CHIPMUNK_N/8]);

// Internal hash functions
int dap_chipmunk_hash_to_point(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen);
int dap_chipmunk_hash_to_seed(uint8_t a_output[32], const uint8_t *a_input, size_t a_inlen);

#endif // _DAP_CHIPMUNK_INTERNAL_H_ 
