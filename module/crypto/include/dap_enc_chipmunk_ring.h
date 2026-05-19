/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Ltd   https://demlabs.net
 * Copyright  (c) 2025-2026
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

/**
 * @file dap_enc_chipmunk_ring.h
 * @brief Public DAP-key surface for the Chipmunk linkable ring signature.
 *
 * Backed by `chipmunk_lrs` — native, quantum-resistant, CLSAG-style
 * linkable ring signature on the Chipmunk lattice substrate.  Single
 * parameter profile, single canonical wire family.  Ring sign / verify
 * are exposed through `dap_sign_create_ring` / `dap_sign_verify_ring`
 * because they require ring context that does not fit the standard
 * single-key sign_get / sign_verify contract.
 *
 * The `dap_enc_key_t` private/public buffers carry, byte-for-byte,
 * `chipmunk_lrs_secret_key_t` / `chipmunk_lrs_public_key_t`.  Serialization
 * callbacks store/restore those structures verbatim.
 */

#include "dap_enc_key.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Per-key public/private buffer sizes — pinned to chipmunk_lrs CLPK/CLSK. */
#define DAP_ENC_CHIPMUNK_RING_PUB_KEY_SIZE   1456u
#define DAP_ENC_CHIPMUNK_RING_PRIV_KEY_SIZE  1488u

/**
 * @brief Module-wide initialiser. Idempotent.
 * @return 0 on success.
 */
int dap_enc_chipmunk_ring_init(void);

/**
 * @brief Allocate an empty dap_enc_key_t of type
 *        DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING.  Buffers are NOT populated.
 */
dap_enc_key_t *dap_enc_chipmunk_ring_key_new(void);

/**
 * @brief Allocate and key-gen.  If @p a_seed is NULL or @p a_seed_size
 *        is 0, a CSPRNG seed is drawn via the SDK randombytes helper.
 *
 * Returns a new dap_enc_key_t with both pub_key_data and priv_key_data
 * populated (sizes DAP_ENC_CHIPMUNK_RING_PUB_KEY_SIZE /
 * DAP_ENC_CHIPMUNK_RING_PRIV_KEY_SIZE respectively), or NULL on failure.
 */
dap_enc_key_t *dap_enc_chipmunk_ring_key_generate(const void *a_kex_buf, size_t a_kex_size,
                                                  const void *a_seed,    size_t a_seed_size,
                                                  const void *a_personalisation, size_t a_personalisation_size);

/* dap_enc_key callback adapters. */
void dap_enc_chipmunk_ring_key_new_callback(dap_enc_key_t *a_key);
void dap_enc_chipmunk_ring_key_generate_callback(dap_enc_key_t *a_key,
                                                 const void *a_kex_buf, size_t a_kex_size,
                                                 const void *a_seed,    size_t a_seed_size,
                                                 size_t a_key_size);
void dap_enc_chipmunk_ring_key_delete(dap_enc_key_t *a_key);

/* Public-key serialization. */
uint8_t *dap_enc_chipmunk_ring_write_public_key(const void *a_key, size_t *a_buflen_out);
void    *dap_enc_chipmunk_ring_read_public_key(const uint8_t *a_buf, size_t a_buflen);
size_t   dap_enc_chipmunk_ring_ser_public_key_size(const void *a_key);
size_t   dap_enc_chipmunk_ring_deser_public_key_size(const void *a_buf);
void     dap_enc_chipmunk_ring_public_key_delete(void *a_pub_key);

/* Private-key serialization. */
uint8_t *dap_enc_chipmunk_ring_write_private_key(const void *a_key, size_t *a_buflen_out);
void    *dap_enc_chipmunk_ring_read_private_key(const uint8_t *a_buf, size_t a_buflen);
size_t   dap_enc_chipmunk_ring_ser_private_key_size(const void *a_key);
size_t   dap_enc_chipmunk_ring_deser_private_key_size(const void *a_buf);
void     dap_enc_chipmunk_ring_private_key_delete(void *a_priv_key);

#ifdef __cplusplus
}
#endif
