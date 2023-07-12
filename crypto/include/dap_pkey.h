/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net    https:/gitlab.com/demlabs
 * Kelvin Project https://github.com/kelvinblockchain
 * Copyright  (c) 2017-2018
 * All rights reserved.

 This file is part of DAP (Deus Applications Prototypes) the open source project

    DAP (Deus Applicaions Prototypes) is free software: you can redistribute it and/or modify
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

#include <stdint.h>
#include "dap_common.h"
#include "dap_enc_key.h"
#include "dap_sign.h"

enum dap_pkey_type_enum {
    PKEY_TYPE_NULL = 0x0000,
    PKEY_TYPE_SIGN_BLISS = 0x0901,
    PKEY_TYPE_SIGN_TESLA = 0x0902,
    PKEY_TYPE_SIGN_DILITHIUM =  0x0903,
    PKEY_TYPE_SIGN_PICNIC = 0x0102,
    PKEY_TYPE_SIGN_FALCON = 0x0103,
    PKEY_TYPE_MULTI = 0xffff ///  @brief Has inside subset of different keys
};
typedef uint16_t dap_pkey_type_enum_t;

typedef union dap_pkey_type{
    dap_pkey_type_enum_t type;
    uint16_t raw;
} dap_pkey_type_t;

DAP_STATIC_INLINE const char *dap_pkey_type_to_str(dap_pkey_type_t a_type){
    switch (a_type.type) {
        case PKEY_TYPE_NULL:  return  "PKEY_TYPE_NULL";
        case PKEY_TYPE_MULTI: return "PKEY_TYPE_MULTI";
        case PKEY_TYPE_SIGN_BLISS: return "PKEY_TYPE_SIGN_BLISS";
        case PKEY_TYPE_SIGN_TESLA: return "PKEY_TYPE_SIGN_TESLA";
        case PKEY_TYPE_SIGN_PICNIC: return "PKEY_TYPE_SIGN_PICNIC";
        case PKEY_TYPE_SIGN_DILITHIUM: return "PKEY_TYPE_SIGN_DILITHIUM";
        case PKEY_TYPE_SIGN_FALCON: return "PKEY_TYPE_SIGN_FALCON";
        default: return "UNDEFINED";
    }
}

/**
 * @brief convert public key type (dap_pkey_type_t) to dap_sign_type_t type
 *
 * @param a_pkey_type dap_pkey_type_t key type
 * @return dap_sign_type_t
 */
DAP_STATIC_INLINE dap_sign_type_t dap_pkey_type_to_sign_type(dap_pkey_type_t a_pkey_type)
{
    dap_sign_type_t l_sign_type = {0};
    switch (a_pkey_type.type){
        case PKEY_TYPE_SIGN_BLISS: l_sign_type.type = SIG_TYPE_BLISS; break;
        case PKEY_TYPE_SIGN_PICNIC: l_sign_type.type = SIG_TYPE_PICNIC; break;
        case PKEY_TYPE_SIGN_TESLA: l_sign_type.type = SIG_TYPE_TESLA; break;
        case PKEY_TYPE_SIGN_DILITHIUM : l_sign_type.type = SIG_TYPE_DILITHIUM; break;
        case PKEY_TYPE_SIGN_FALCON : l_sign_type.type = SIG_TYPE_FALCON; break;
        case PKEY_TYPE_MULTI: l_sign_type.type = SIG_TYPE_MULTI_CHAINED; break;
        default: l_sign_type.type = SIG_TYPE_NULL; break;
    }
    return l_sign_type;
}

/**
 * @brief convert dap_sign_type_t type to public key type (dap_pkey_type_t)
 *
 * @param a_sign_type dap_sign_type_t key type
 * @return dap_pkey_type_t
 */
DAP_STATIC_INLINE dap_pkey_type_t dap_pkey_type_from_sign_type(dap_sign_type_t a_sign_type)
{
    dap_pkey_type_t l_pkey_type = {0};
    switch (a_sign_type.type){
        case SIG_TYPE_BLISS: l_pkey_type.type = PKEY_TYPE_SIGN_BLISS; break;
        case SIG_TYPE_PICNIC: l_pkey_type.type = PKEY_TYPE_SIGN_PICNIC; break;
        case SIG_TYPE_TESLA: l_pkey_type.type = PKEY_TYPE_SIGN_TESLA; break;
        case SIG_TYPE_DILITHIUM: l_pkey_type.type = PKEY_TYPE_SIGN_DILITHIUM; break;
        case SIG_TYPE_FALCON: l_pkey_type.type = PKEY_TYPE_SIGN_FALCON; break;
        case SIG_TYPE_MULTI_CHAINED: l_pkey_type.type = PKEY_TYPE_MULTI; break;
        default: l_pkey_type.type = PKEY_TYPE_NULL; break;
    }
    return l_pkey_type;
}

/**
 * @brief convert public key type (dap_pkey_type_t) to dap_enc_key_type_t type
 *
 * @param a_pkey_type dap_pkey_type_t key type
 * @return dap_enc_key_type_t
 */
DAP_STATIC_INLINE dap_enc_key_type_t dap_pkey_type_to_enc_key_type(dap_pkey_type_t a_pkey_type)
{
    switch (a_pkey_type.type){
        case PKEY_TYPE_SIGN_BLISS: return DAP_ENC_KEY_TYPE_SIG_BLISS;
        case PKEY_TYPE_SIGN_PICNIC: return DAP_ENC_KEY_TYPE_SIG_PICNIC;
        case PKEY_TYPE_SIGN_TESLA: return DAP_ENC_KEY_TYPE_SIG_TESLA;
        case PKEY_TYPE_SIGN_DILITHIUM: return DAP_ENC_KEY_TYPE_SIG_DILITHIUM;
        case PKEY_TYPE_SIGN_FALCON: return DAP_ENC_KEY_TYPE_SIG_FALCON;
        default:;
    }
    return DAP_ENC_KEY_TYPE_INVALID;
}

/**
 * @brief convert dap_enc_key_type_t type to public key type (dap_pkey_type_t)
 *
 * @param a_key_type dap_enc_key_type_t key type
 * @return dap_pkey_type_t
 */
DAP_STATIC_INLINE dap_pkey_type_t dap_pkey_type_from_enc_key_type(dap_enc_key_type_t a_key_type)
{
    dap_pkey_type_t l_pkey_type={0};
    switch (a_key_type){
        case DAP_ENC_KEY_TYPE_SIG_BLISS: l_pkey_type.type = PKEY_TYPE_SIGN_BLISS; break;
        case DAP_ENC_KEY_TYPE_SIG_PICNIC: l_pkey_type.type = PKEY_TYPE_SIGN_PICNIC; break;
        case DAP_ENC_KEY_TYPE_SIG_TESLA: l_pkey_type.type = PKEY_TYPE_SIGN_TESLA; break;
        case DAP_ENC_KEY_TYPE_SIG_DILITHIUM: l_pkey_type.type = PKEY_TYPE_SIGN_DILITHIUM; break;
        case DAP_ENC_KEY_TYPE_SIG_FALCON: l_pkey_type.type = PKEY_TYPE_SIGN_FALCON; break;
        default: l_pkey_type.type = PKEY_TYPE_NULL; break;
    }
    return l_pkey_type;
}

/**
  * @struct dap_pkey
  * @brief Public keys
  */
typedef struct dap_pkey{
    struct {
        dap_pkey_type_t type; /// Pkey type
        uint32_t size; /// Pkey size
    } header; /// Only header's hash is used for verification
    uint8_t pkey[]; /// @param pkey @brief raw pkey dat
} DAP_ALIGN_PACKED dap_pkey_t;

dap_pkey_t *dap_pkey_from_enc_key(dap_enc_key_t *a_key);

bool dap_pkey_match(dap_pkey_t *a_pkey1, dap_pkey_t *a_pkey2);

bool dap_pkey_match_sign(dap_pkey_t *a_pkey, dap_sign_t *a_sign);

bool dap_pkey_get_hash(dap_pkey_t *a_pkey, dap_chain_hash_fast_t *a_out_hash);

DAP_STATIC_INLINE bool dap_pkey_compare_with_sign(dap_pkey_t *a_pkey, dap_sign_t *a_sign)
{
    return (dap_pkey_type_to_enc_key_type(a_pkey->header.type) == dap_sign_type_to_key_type(a_sign->header.type) &&
            a_pkey->header.size == a_sign->header.sign_pkey_size &&
            !memcmp(a_pkey->pkey, a_sign->pkey_n_sign, a_pkey->header.size));
}

DAP_STATIC_INLINE bool dap_pkey_compare(dap_pkey_t *a_pkey1, dap_pkey_t *a_pkey2)
{
    return (a_pkey1->header.type.raw == a_pkey2->header.type.raw &&
            a_pkey1->header.size == a_pkey2->header.size &&
            !memcmp(a_pkey1->pkey, a_pkey2->pkey, a_pkey1->header.size));
}

dap_pkey_t *dap_pkey_get_from_sign_deserialization(dap_sign_t *a_sign);
