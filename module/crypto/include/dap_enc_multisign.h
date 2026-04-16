/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net    https:/gitlab.com/demlabs
 * Kelvin Project https://github.com/kelvinblockchain
 * Copyright  (c) 2017-2018
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

#pragma once

#include "dap_sign.h"
#include "dap_math_ops.h"
#include "dap_serialize.h"

typedef struct _dap_multi_sign_params_t {
    dap_sign_type_t type;               // Multi-signature type
    uint8_t key_count;                // Total key count
    uint8_t sign_count;                 // Signatures count
    uint8_t *key_seq;                   // Signing key sequence
    dap_enc_key_t **keys;               // Signing keys
} dap_multi_sign_params_t;

typedef struct dap_multi_sign_meta {
    dap_sign_hdr_t sign_header;         // header data need to verification
} DAP_ALIGN_PACKED dap_multi_sign_meta_t;
DAP_STATIC_ASSERT(sizeof(dap_multi_sign_meta_t) == sizeof(dap_sign_hdr_t),
                  "dap_multi_sign_meta_t packed wire size");

typedef dap_sign_hdr_mem_t dap_multi_sign_meta_mem_t;
extern const dap_serialize_schema_t g_dap_multi_sign_meta_schema;
#define DAP_MULTI_SIGN_META_MAGIC 0xDA5FEEDEU

DAP_STATIC_INLINE int dap_multi_sign_meta_pack(const dap_multi_sign_meta_mem_t *a_mem,
                                               uint8_t *a_wire, size_t a_wire_size)
{
    if (a_wire_size < DAP_SIGN_HDR_WIRE_SIZE) return -1;
    dap_serialize_result_t r = dap_serialize_to_buffer_raw(
        &g_dap_multi_sign_meta_schema, a_mem, a_wire, a_wire_size, NULL);
    return r.error_code;
}

DAP_STATIC_INLINE int dap_multi_sign_meta_unpack(const uint8_t *a_wire, size_t a_wire_size,
                                                 dap_multi_sign_meta_mem_t *a_mem)
{
    if (a_wire_size < DAP_SIGN_HDR_WIRE_SIZE) return -1;
    dap_deserialize_result_t r = dap_deserialize_from_buffer_raw(
        &g_dap_multi_sign_meta_schema, a_wire, a_wire_size, a_mem, NULL);
    return r.error_code;
}

typedef struct dap_multi_sign {
/*** Hashed metadata ***/
    dap_sign_type_t type;               // Multi-signature type
    uint8_t key_count;                  // Total key count
    uint8_t sign_count;                 // Signatures count
    uint8_t *key_seq;     // Signing key sequence
/*** Unhashed metadata ***/
    dap_multi_sign_meta_t *meta;        // Sizes of keys and signatures
/*** Key hashes ***/
    dap_hash_sha3_256_t *key_hashes;  // Total key hashes
/*** Serialized signatures chain ***/
    uint8_t *sign_data;                 // Signatures data
} dap_multi_sign_t;

typedef struct dap_multisign_private_key {
  uint64_t len;                 
  uint8_t data[];
} dap_multisign_private_key_t;

typedef struct dap_multisign_public_key{
  uint64_t len;                 
  uint8_t data[];
} DAP_ALIGN_PACKED dap_multisign_public_key_t;

typedef struct dap_multisign_pub_key_hdr_mem {
    uint64_t len;
} dap_multisign_pub_key_hdr_mem_t;

#define DAP_MULTISIGN_PUB_KEY_HDR_WIRE_SIZE (sizeof(uint64_t))

extern const dap_serialize_field_t g_dap_multisign_pub_key_hdr_fields[];
extern const dap_serialize_schema_t g_dap_multisign_pub_key_hdr_schema;
#define DAP_MULTISIGN_PUB_KEY_HDR_MAGIC 0xDA5FEEE0U

DAP_STATIC_INLINE int dap_multisign_pub_key_hdr_pack(const dap_multisign_pub_key_hdr_mem_t *a_mem,
                                                     uint8_t *a_wire, size_t a_wire_size)
{
    if (a_wire_size < DAP_MULTISIGN_PUB_KEY_HDR_WIRE_SIZE) return -1;
    dap_serialize_result_t r = dap_serialize_to_buffer_raw(
        &g_dap_multisign_pub_key_hdr_schema, a_mem, a_wire, a_wire_size, NULL);
    return r.error_code;
}

DAP_STATIC_INLINE int dap_multisign_pub_key_hdr_unpack(const uint8_t *a_wire, size_t a_wire_size,
                                                       dap_multisign_pub_key_hdr_mem_t *a_mem)
{
    if (a_wire_size < DAP_MULTISIGN_PUB_KEY_HDR_WIRE_SIZE) return -1;
    dap_deserialize_result_t r = dap_deserialize_from_buffer_raw(
        &g_dap_multisign_pub_key_hdr_schema, a_wire, a_wire_size, a_mem, NULL);
    return r.error_code;
}

#ifdef __cplusplus
extern "C" {
#endif

void dap_enc_sig_multisign_key_new(dap_enc_key_t *a_key);
void dap_enc_sig_multisign_key_new_generate(dap_enc_key_t *a_key, const void *a_kex_buf, size_t a_kex_size,
        const void *a_seed, size_t a_seed_size, size_t a_key_size);

dap_multi_sign_params_t *dap_multi_sign_params_make(dap_sign_type_enum_t a_type, dap_enc_key_t **a_keys, uint8_t a_key_count, const int *a_key_seq, uint8_t a_sign_count);
void dap_multi_sign_params_delete(dap_multi_sign_params_t *a_params);
int dap_enc_sig_multisign_get_sign(dap_enc_key_t *a_key, const void *a_msg_in, const size_t a_msg_size,
        void *a_sign_out, const size_t a_out_size_max);
int dap_enc_sig_multisign_verify_sign(dap_enc_key_t *a_key, const void *a_msg, const size_t a_msg_size, void *a_sign,
        const size_t a_sign_size);
void dap_multi_sign_delete(void *a_sign);
void dap_enc_sig_multisign_key_delete(dap_enc_key_t *a_key);
int dap_enc_sig_multisign_forming_keys(dap_enc_key_t *a_key, const dap_multi_sign_params_t *a_params);

uint8_t *dap_enc_sig_multisign_write_signature(const void *a_sign, size_t *a_out_len);
void *dap_enc_sig_multisign_read_signature(const uint8_t *a_sign, size_t a_sign_len);

uint64_t dap_enc_sig_multisign_ser_priv_key_size(const void *a_priv_key);
uint64_t dap_enc_sig_multisign_ser_pub_key_size(const void *a_pub_key);

DAP_STATIC_INLINE uint64_t dap_enc_sig_multisign_deser_sig_size(UNUSED_ARG const void *a_in)
{
    return sizeof(dap_multi_sign_t);
}

#ifdef __cplusplus
}
#endif
