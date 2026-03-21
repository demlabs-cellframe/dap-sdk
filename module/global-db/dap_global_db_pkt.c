/*
* Authors:
* Roman Khlopkov <roman.khlopkov@demlabs.net>
* Cellframe       https://cellframe.net
* DeM Labs Inc.   https://demlabs.net
* Copyright  (c) 2017-2023
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

#include <string.h>
#include "dap_global_db_pkt.h"
#include "dap_sign.h"
#include "dap_strfuncs.h"
#include "dap_crc64.h"

#define LOG_TAG "dap_global_db_pkt"

/**
 * @brief Multiples data into a_old_pkt structure from a_new_pkt structure.
 * @param a_old_pkt a pointer to the old object
 * @param a_new_pkt a pointer to the new object
 * @return Returns a pointer to the multiple object
 */
dap_global_db_pkt_pack_t *dap_global_db_pkt_pack(dap_global_db_pkt_pack_t *a_old_pkt, dap_global_db_pkt_t *a_new_pkt)
{
    if (!a_new_pkt)
        return a_old_pkt;
    size_t l_add_size = dap_global_db_pkt_get_size(a_new_pkt);
    dap_global_db_pkt_pack_t *l_old_pkt = !a_old_pkt
        ? DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_global_db_pkt_pack_t, sizeof(dap_global_db_pkt_pack_t) + l_add_size, NULL)
        : DAP_REALLOC_RET_VAL_IF_FAIL(a_old_pkt, a_old_pkt->data_size + sizeof(dap_global_db_pkt_pack_t) + l_add_size, NULL);
    memcpy(l_old_pkt->data + l_old_pkt->data_size, a_new_pkt, l_add_size);
    l_old_pkt->data_size += l_add_size;
    ++l_old_pkt->obj_count;
    return l_old_pkt;
}

/**
 * @brief Serializes an object into a packed structure.
 * @param a_store_obj a pointer to the object to be serialized
 * @return Returns a pointer to the packed sructure if successful, otherwise NULL.
 */
dap_global_db_pkt_t *dap_global_db_pkt_serialize(dap_global_db_store_obj_t *a_store_obj)
{
    dap_return_val_if_fail(a_store_obj, NULL);
    size_t l_group_len = dap_strlen(a_store_obj->group);
    size_t l_key_len = dap_strlen(a_store_obj->key);
    size_t l_sign_len = a_store_obj->sign ? dap_sign_get_size(a_store_obj->sign) : 0;
    size_t l_data_size_out = l_group_len + l_key_len + a_store_obj->value_len + l_sign_len;
    dap_global_db_pkt_t *l_pkt = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_global_db_pkt_t, l_data_size_out + sizeof(dap_global_db_pkt_t), NULL);

    /* Fill packet header */
    l_pkt->timestamp = a_store_obj->timestamp;
    l_pkt->group_len = l_group_len;
    l_pkt->key_len = l_key_len;
    l_pkt->value_len = a_store_obj->value_len;
    l_pkt->crc = a_store_obj->crc;
    l_pkt->flags = a_store_obj->flags & DAP_GLOBAL_DB_RECORD_DEL;
    l_pkt->data_len = l_data_size_out;

    /* Put serialized data into the payload part of the packet */
    char *l_data_ptr = (char *)l_pkt->data;
    l_data_ptr = dap_mempcpy(l_data_ptr, a_store_obj->group, l_group_len);
    l_data_ptr = dap_mempcpy(l_data_ptr, a_store_obj->key, l_key_len);
    if (a_store_obj->value_len)
        l_data_ptr = dap_mempcpy(l_data_ptr, a_store_obj->value, a_store_obj->value_len);
    if (a_store_obj->sign)
        l_data_ptr = dap_mempcpy(l_data_ptr, a_store_obj->sign, l_sign_len);

    assert((size_t)((byte_t *)l_data_ptr - l_pkt->data) == l_data_size_out);
    return l_pkt;
}

dap_sign_t *dap_global_db_store_obj_sign(dap_global_db_store_obj_t *a_obj, dap_enc_key_t *a_key, uint64_t *a_checksum)
{
    dap_return_val_if_fail(a_obj, NULL);
    dap_global_db_pkt_t *l_pkt = dap_global_db_pkt_serialize(a_obj);
    if (!l_pkt) {
        log_it(L_ERROR, "Can't serialize global DB object");
        return NULL;
    }
    dap_sign_t *l_sign = NULL;
    if (a_key) {
        // Exclude CRC field from sign
        l_sign = dap_sign_create(a_key, (uint8_t *)l_pkt + sizeof(uint64_t),
                                 dap_global_db_pkt_get_size(l_pkt) - sizeof(uint64_t));
        if (!l_sign) {
            log_it(L_ERROR, "Can't sign serialized global DB object");
            DAP_DELETE(l_pkt);
            return NULL;
        }
    }
    if (a_checksum) {
        if (a_key) {
            size_t l_sign_len = dap_sign_get_size(l_sign);
            dap_global_db_pkt_t *l_new_pkt = DAP_REALLOC_RET_VAL_IF_FAIL(l_pkt, dap_global_db_pkt_get_size(l_pkt) + l_sign_len, NULL, l_sign);
            l_pkt = l_new_pkt;
            memcpy(l_pkt->data + l_pkt->data_len, l_sign, l_sign_len);
            l_pkt->data_len += l_sign_len;
        }
        *a_checksum = crc64((uint8_t *)l_pkt + sizeof(uint64_t),
                            dap_global_db_pkt_get_size(l_pkt) - sizeof(uint64_t));
    }
    DAP_DELETE(l_pkt);
    return l_sign;
}

/// Consume all security checks passed before by object deserializing
bool dap_global_db_pkt_check_sign_crc(dap_global_db_store_obj_t *a_obj)
{
    dap_return_val_if_fail(a_obj, false);
    dap_global_db_pkt_t *l_pkt = dap_global_db_pkt_serialize(a_obj);
    if (!l_pkt)
        return false;
    size_t l_signed_data_size = l_pkt->group_len + l_pkt->key_len + l_pkt->value_len;
    size_t l_full_data_len = l_pkt->data_len;
    l_pkt->data_len = l_signed_data_size;
    dap_sign_t *l_sign = (dap_sign_t *)(l_pkt->data + l_signed_data_size);
    if (a_obj->sign && dap_sign_verify(l_sign, (uint8_t *)l_pkt + sizeof(uint64_t),
                                        dap_global_db_pkt_get_size(l_pkt) - sizeof(uint64_t))) {
        DAP_DELETE(l_pkt);
        return false;
    }
    l_pkt->data_len = l_full_data_len;
    uint64_t l_checksum = crc64((uint8_t *)l_pkt + sizeof(uint64_t),
                                dap_global_db_pkt_get_size(l_pkt) - sizeof(uint64_t));
    bool ret = l_checksum == l_pkt->crc;
    DAP_DELETE(l_pkt);
    return ret;

}

/**
 * @brief Batch signature verification + CRC check for an array of store objects.
 *
 * Groups signed objects by signature type, uses dap_sign_batch_verify for
 * Dilithium/Chipmunk when the group is large enough, falls back to individual
 * verification otherwise.  CRC is always checked per-object.
 *
 * @param a_objs   Array of store objects
 * @param a_count  Number of objects
 * @param a_results  Per-object boolean results (true = ok)
 * @return Number of objects that passed, or -1 on allocation error
 */
int dap_global_db_pkt_batch_check_sign_crc(dap_global_db_store_obj_t *a_objs,
                                           uint32_t a_count, bool *a_results)
{
    if (!a_objs || !a_count || !a_results)
        return -1;

    dap_global_db_pkt_t **l_pkts = DAP_NEW_Z_COUNT(dap_global_db_pkt_t *, a_count);
    if (!l_pkts) return -1;

    for (uint32_t i = 0; i < a_count; i++) {
        a_results[i] = false;
        l_pkts[i] = dap_global_db_pkt_serialize(a_objs + i);
    }

    typedef struct { uint32_t *indices; uint32_t count; } sig_group_t;
    sig_group_t l_dilithium = { NULL, 0 };
    sig_group_t l_chipmunk  = { NULL, 0 };

    uint32_t l_signed_count = 0;
    for (uint32_t i = 0; i < a_count; i++) {
        if (a_objs[i].sign) l_signed_count++;
    }

    if (l_signed_count > 0) {
        l_dilithium.indices = DAP_NEW_Z_COUNT(uint32_t, l_signed_count);
        l_chipmunk.indices  = DAP_NEW_Z_COUNT(uint32_t, l_signed_count);
    }

    int l_passed = 0;
    for (uint32_t i = 0; i < a_count; i++) {
        if (!l_pkts[i]) continue;

        if (!a_objs[i].sign) {
            uint64_t l_crc = crc64((uint8_t *)l_pkts[i] + sizeof(uint64_t),
                                   dap_global_db_pkt_get_size(l_pkts[i]) - sizeof(uint64_t));
            a_results[i] = (l_crc == l_pkts[i]->crc);
            if (a_results[i]) l_passed++;
            continue;
        }

        size_t l_signed_data_size = l_pkts[i]->group_len + l_pkts[i]->key_len + l_pkts[i]->value_len;
        dap_sign_t *l_sign = (dap_sign_t *)(l_pkts[i]->data + l_signed_data_size);
        dap_sign_type_t l_type = l_sign->header.type;

        if (l_type.type == SIG_TYPE_DILITHIUM || l_type.type == SIG_TYPE_ML_DSA) {
            if (l_dilithium.indices)
                l_dilithium.indices[l_dilithium.count++] = i;
        } else if (l_type.type == SIG_TYPE_CHIPMUNK) {
            if (l_chipmunk.indices)
                l_chipmunk.indices[l_chipmunk.count++] = i;
        } else {
            size_t l_full_data_len = l_pkts[i]->data_len;
            l_pkts[i]->data_len = (uint32_t)l_signed_data_size;
            int l_rc = dap_sign_verify(l_sign, (uint8_t *)l_pkts[i] + sizeof(uint64_t),
                                       dap_global_db_pkt_get_size(l_pkts[i]) - sizeof(uint64_t));
            l_pkts[i]->data_len = (uint32_t)l_full_data_len;
            if (l_rc != 0) goto next_obj;

            uint64_t l_crc = crc64((uint8_t *)l_pkts[i] + sizeof(uint64_t),
                                   dap_global_db_pkt_get_size(l_pkts[i]) - sizeof(uint64_t));
            a_results[i] = (l_crc == l_pkts[i]->crc);
            if (a_results[i]) l_passed++;
        }
next_obj:;
    }

    /* Process batched groups using dap_sign_batch_verify_execute */
    sig_group_t *l_groups[] = { &l_dilithium, &l_chipmunk };
    dap_sign_type_enum_t l_types[] = { SIG_TYPE_DILITHIUM, SIG_TYPE_CHIPMUNK };

    for (int g = 0; g < 2; g++) {
        sig_group_t *l_grp = l_groups[g];
        if (!l_grp->count) continue;

        dap_sign_type_t l_sig_type = { .type = l_types[g] };
        dap_sign_batch_verify_ctx_t *l_ctx = dap_sign_batch_verify_ctx_new(l_sig_type, l_grp->count);
        if (!l_ctx) {
            for (uint32_t j = 0; j < l_grp->count; j++) {
                uint32_t i = l_grp->indices[j];
                size_t l_sd = l_pkts[i]->group_len + l_pkts[i]->key_len + l_pkts[i]->value_len;
                dap_sign_t *l_sign = (dap_sign_t *)(l_pkts[i]->data + l_sd);
                uint32_t l_full = l_pkts[i]->data_len;
                l_pkts[i]->data_len = (uint32_t)l_sd;
                int l_rc = dap_sign_verify(l_sign, (uint8_t *)l_pkts[i] + sizeof(uint64_t),
                                           dap_global_db_pkt_get_size(l_pkts[i]) - sizeof(uint64_t));
                l_pkts[i]->data_len = l_full;
                if (l_rc != 0) continue;
                uint64_t l_crc = crc64((uint8_t *)l_pkts[i] + sizeof(uint64_t),
                                       dap_global_db_pkt_get_size(l_pkts[i]) - sizeof(uint64_t));
                a_results[i] = (l_crc == l_pkts[i]->crc);
                if (a_results[i]) l_passed++;
            }
            continue;
        }

        for (uint32_t j = 0; j < l_grp->count; j++) {
            uint32_t i = l_grp->indices[j];
            size_t l_sd = l_pkts[i]->group_len + l_pkts[i]->key_len + l_pkts[i]->value_len;
            dap_sign_t *l_sign = (dap_sign_t *)(l_pkts[i]->data + l_sd);
            uint32_t l_full = l_pkts[i]->data_len;
            l_pkts[i]->data_len = (uint32_t)l_sd;
            size_t l_msg_size = dap_global_db_pkt_get_size(l_pkts[i]) - sizeof(uint64_t);
            dap_sign_batch_verify_add_signature(l_ctx, l_sign,
                (uint8_t *)l_pkts[i] + sizeof(uint64_t), l_msg_size, NULL);
            l_pkts[i]->data_len = l_full;
        }

        int l_batch_rc = dap_sign_batch_verify_execute(l_ctx);
        dap_sign_batch_verify_ctx_free(l_ctx);

        if (l_batch_rc == 0) {
            for (uint32_t j = 0; j < l_grp->count; j++) {
                uint32_t i = l_grp->indices[j];
                uint64_t l_crc = crc64((uint8_t *)l_pkts[i] + sizeof(uint64_t),
                                       dap_global_db_pkt_get_size(l_pkts[i]) - sizeof(uint64_t));
                a_results[i] = (l_crc == l_pkts[i]->crc);
                if (a_results[i]) l_passed++;
            }
        } else {
            for (uint32_t j = 0; j < l_grp->count; j++) {
                uint32_t i = l_grp->indices[j];
                size_t l_sd = l_pkts[i]->group_len + l_pkts[i]->key_len + l_pkts[i]->value_len;
                dap_sign_t *l_sign = (dap_sign_t *)(l_pkts[i]->data + l_sd);
                uint32_t l_full = l_pkts[i]->data_len;
                l_pkts[i]->data_len = (uint32_t)l_sd;
                int l_rc = dap_sign_verify(l_sign, (uint8_t *)l_pkts[i] + sizeof(uint64_t),
                                           dap_global_db_pkt_get_size(l_pkts[i]) - sizeof(uint64_t));
                l_pkts[i]->data_len = l_full;
                if (l_rc != 0) continue;
                uint64_t l_crc = crc64((uint8_t *)l_pkts[i] + sizeof(uint64_t),
                                       dap_global_db_pkt_get_size(l_pkts[i]) - sizeof(uint64_t));
                a_results[i] = (l_crc == l_pkts[i]->crc);
                if (a_results[i]) l_passed++;
            }
        }
    }

    DAP_DEL_Z(l_dilithium.indices);
    DAP_DEL_Z(l_chipmunk.indices);
    for (uint32_t i = 0; i < a_count; i++)
        DAP_DEL_Z(l_pkts[i]);
    DAP_DELETE(l_pkts);
    return l_passed;
}

static byte_t *s_fill_one_store_obj(dap_global_db_pkt_t *a_pkt, dap_global_db_store_obj_t *a_obj, size_t a_bound_size, dap_cluster_node_addr_t *a_addr)
{
    if (sizeof(dap_global_db_pkt_t) > a_bound_size ||            /* Check for buffer boundaries */
            dap_global_db_pkt_get_size(a_pkt) > a_bound_size ||
            a_pkt->group_len + a_pkt->key_len + a_pkt->value_len < a_pkt->value_len ||
            a_pkt->group_len + a_pkt->key_len + a_pkt->value_len > a_pkt->data_len) {
        log_it(L_ERROR, "Broken GDB element: size is incorrect");
        return NULL;
    }
    if (!a_pkt->group_len || a_pkt->group_len > DAP_GLOBAL_DB_GROUP_NAME_SIZE_MAX) {
        log_it(L_ERROR, "Broken GDB element: 'group_len' field is incorrect");
        return NULL;
    }
    if (!a_pkt->key_len || a_pkt->key_len > DAP_GLOBAL_DB_KEY_SIZE_MAX) {
        log_it(L_ERROR, "Broken GDB element: 'key_len' field is incorrect");
        return NULL;
    }
    a_obj->flags = a_pkt->flags & DAP_GLOBAL_DB_RECORD_DEL;
    a_obj->timestamp = a_pkt->timestamp;
    a_obj->value_len = a_pkt->value_len;
    a_obj->crc = a_pkt->crc;
    byte_t *l_data_ptr = a_pkt->data;

    a_obj->group = strndup((char*)l_data_ptr, a_pkt->group_len);
    if (!a_obj->group) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    l_data_ptr += a_pkt->group_len;

    a_obj->key = strndup((char*)l_data_ptr, a_pkt->key_len);
    if (!a_obj->key) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(a_obj->group);
        return NULL;
    }
    l_data_ptr += a_pkt->key_len;

    if (a_pkt->value_len) {
        a_obj->value = DAP_DUP_SIZE_RET_VAL_IF_FAIL(l_data_ptr, a_pkt->value_len, NULL, a_obj->group, a_obj->key);
        l_data_ptr += a_pkt->value_len;
    }

    size_t l_sign_size_expected = a_pkt->data_len - a_pkt->group_len - a_pkt->key_len - a_pkt->value_len;
    if (l_sign_size_expected) {
        dap_sign_t *l_sign = (dap_sign_t *)l_data_ptr;
        size_t l_sign_size = dap_sign_get_size(l_sign);
        if (l_sign_size != l_sign_size_expected) {
            log_it(L_ERROR, "Broken GDB element: sign size %zu isn't equal expected size %zu", l_sign_size, l_sign_size_expected);
            DAP_DELETE(a_obj->group);
            DAP_DELETE(a_obj->key);
            DAP_DEL_Z(a_obj->value);
            return NULL;
        }
        a_obj->sign = DAP_DUP_SIZE_RET_VAL_IF_FAIL(l_sign, l_sign_size, NULL, a_obj->group, a_obj->key, a_obj->value);
    }

    if (a_addr)
        *(dap_cluster_node_addr_t *)a_obj->ext = *a_addr;

    return l_data_ptr + l_sign_size_expected;
}


dap_global_db_store_obj_t *dap_global_db_pkt_deserialize(dap_global_db_pkt_t *a_pkt, size_t a_pkt_size, dap_cluster_node_addr_t *a_addr)
{
    dap_return_val_if_fail(a_pkt, NULL);
    dap_global_db_store_obj_t *l_ret = DAP_NEW_Z_SIZE(dap_global_db_store_obj_t, sizeof(dap_global_db_store_obj_t) + sizeof(*a_addr));
    if (!l_ret)
        log_it(L_CRITICAL, "Memory allocation_error");
    else if (!s_fill_one_store_obj(a_pkt, l_ret, a_pkt_size, a_addr)) {
        log_it(L_ERROR, "Broken GDB element: can't read GOSSIP record packet");
        DAP_DEL_Z(l_ret);
    }
    return l_ret;
}

/**
 * @brief Deserializes some objects from a packed structure into an array of objects.
 * @param pkt a pointer to the serialized packed structure
 * @param store_obj_count[out] a number of deserialized objects in the array
 * @return Returns a pointer to the first object in the array, if successful; otherwise NULL.
 */
#ifdef DAP_GLOBAL_DB_WRITE_SERIALIZED
dap_global_db_store_obj_t *dap_global_db_pkt_pack_deserialize(dap_global_db_pkt_pack_t *a_pkt, size_t *a_store_obj_count)
#else
dap_global_db_store_obj_t **dap_global_db_pkt_pack_deserialize(dap_global_db_pkt_pack_t *a_pkt, size_t *a_store_obj_count, dap_cluster_node_addr_t *a_addr)
#endif
{
    dap_return_val_if_fail(a_pkt && a_pkt->data_size >= sizeof(dap_global_db_pkt_t), NULL);

    uint32_t l_count = a_pkt->obj_count;
    size_t l_size = l_count <= DAP_GLOBAL_DB_PKT_PACK_MAX_COUNT
            ? l_count *
#ifdef DAP_GLOBAL_DB_WRITE_SERIALIZED
              sizeof(dap_global_db_store_obj_t)
#else
              sizeof(dap_global_db_store_obj_t *)
#endif
            : 0;

    if (!l_size) {
        log_it(L_ERROR, "Invalid size: packet pack total size is zero");
        return NULL;
    }
#ifdef DAP_GLOBAL_DB_WRITE_SERIALIZED
    dap_global_db_store_obj_t *l_store_obj_arr = DAP_NEW_Z_SIZE(dap_global_db_store_obj_t, l_size);
#else
    dap_global_db_store_obj_t **l_store_obj_arr = DAP_NEW_Z_SIZE(dap_global_db_store_obj_t *, l_size);
#endif
    if (!l_store_obj_arr) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }

    byte_t *l_data_ptr = (byte_t *)a_pkt->data;                                 /* Set <l_data_ptr> to begin of payload */
    byte_t *l_data_end = l_data_ptr + a_pkt->data_size;                         /* Set <l_data_end> to end of payload area
                                                                                will be used to prevent out-of-buffer case */
    uint32_t i = 0;
    for ( ; i < l_count; i++) {
#ifdef DAP_GLOBAL_DB_WRITE_SERIALIZED
        l_data_ptr = s_fill_one_store_obj((dap_global_db_pkt_t *)l_data_ptr, l_store_obj_arr + i, l_data_end - l_data_ptr, NULL);
#else
        l_store_obj_arr[i] = DAP_NEW_Z_SIZE(dap_global_db_store_obj_t, sizeof(dap_global_db_store_obj_t) + sizeof(dap_cluster_node_addr_t));
        if (!l_store_obj_arr[i]) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            break;
        }
        l_data_ptr = s_fill_one_store_obj((dap_global_db_pkt_t *)l_data_ptr, l_store_obj_arr[i], l_data_end - l_data_ptr, a_addr);
#endif
        if (!l_data_ptr) {
            log_it(L_ERROR, "Broken GDB element: can't read packet #%u", i);
            break;
        }
    }
    if (l_data_ptr)
        assert(l_data_ptr == l_data_end);
    // Return the number of completely filled dap_global_db_store_obj_t structures
    if (a_store_obj_count)
        *a_store_obj_count = i;
    if (!i)
        DAP_DEL_Z(l_store_obj_arr);

    return l_store_obj_arr;
}
