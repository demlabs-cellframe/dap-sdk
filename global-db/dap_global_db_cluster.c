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

#include "dap_global_db_cluster.h"
#include "dap_strfuncs.h"
#include "dap_sign.h"
#include "crc32c_adler/crc32c_adler.h"

int dap_global_db_cluster_init()
{

}

void dap_global_db_cluster_deinit()
{

}

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
    dap_global_db_pkt_pack_t *l_old_pkt;
    if (a_old_pkt)
        l_old_pkt = (dap_global_db_pkt_pack_t *)DAP_REALLOC(a_old_pkt, a_old_pkt->data_size + sizeof(dap_global_db_pkt_pack_t) + l_add_size);
    else
        l_old_pkt = DAP_NEW_Z_SIZE(dap_global_db_pkt_pack_t, sizeof(dap_global_db_pkt_pack_t) + l_add_size);
    memcpy(l_old_pkt->data + l_old_pkt->data_size, l_new_pkt, l_add_size);
    l_old_pkt->data_size += l_add_size;
    l_old_pkt->obj_count++;
    return l_old_pkt;
}

/**
 * @brief Serializes an object into a packed structure.
 * @param a_store_obj a pointer to the object to be serialized
 * @return Returns a pointer to the packed sructure if successful, otherwise NULL.
 */
dap_global_db_pkt_t *dap_global_db_pkt_serialize(dap_store_obj_t *a_store_obj)
{
    dap_return_val_if_fail(a_store_obj, NULL);

    size_t l_group_len = dap_strlen(a_store_obj->group);
    size_t l_key_len = dap_strlen(a_store_obj->key);
    size_t l_sign_len = dap_sign_get_size(a_store_obj->sign);
    size_t l_data_size_out = l_group_len + l_key_len + a_store_obj->value_len + l_sign_len;
    dap_global_db_pkt_t *l_pkt = DAP_NEW_SIZE(dap_global_db_pkt_t, l_data_size_out + sizeof(dap_global_db_pkt_t));
    if (!l_pkt) {
        log_it(L_CRITICAL, "Insufficient memory");
        return NULL;
    }

    /* Fill packet header */
    l_pkt->timestamp = a_store_obj->timestamp;
    l_pkt->group_len = l_group_len;
    l_pkt->key_len = l_key_len;
    l_pkt->value_len = a_store_obj->value_len;
    l_pkt->crc = a_store_obj->crc;
    l_pkt->total_len = l_data_size_out;

    /* Put serialized data into the payload part of the packet */
    byte_t *l_data_ptr = l_pkt->data;
    l_data_ptr = dap_stpcpy(l_data_ptr, a_store_obj->group);
    l_data_ptr = dap_stpcpy(l_data_ptr, a_store_obj->key);
    if (a_store_obj->value_len)
        l_data_ptr = mempcpy(l_data_ptr, a_store_obj->value, a_store_obj->value_len);
    l_data_ptr = mempcpy(l_data_ptr, a_store_obj->sign, l_sign_len);

    assert(l_data_ptr - l_pkt->data == l_data_size_out);
    return l_pkt;
}

uint32_t dap_store_obj_checksum(dap_store_obj_t *a_obj)
{
    dap_global_db_pkt_t *l_pkt = dap_global_db_pkt_serialize(a_obj);
    if (!l_pkt) {
        log_it(L_ERROR, "Can't serialize global DB object");
        return 0;
    }
    uint32_t l_ret = crc32c(CRC32C_INIT,
                            (byte_t *)l_pkt + sizeof(uint32_t),
                            dap_global_db_pkt_get_size(l_pkt) - sizeof(uint32_t));
    DAP_DELETE(l_pkt);
    return l_ret;
}

/**
 * @brief Deserializes some objects from a packed structure into an array of objects.
 * @param pkt a pointer to the serialized packed structure
 * @param store_obj_count[out] a number of deserialized objects in the array
 * @return Returns a pointer to the first object in the array, if successful; otherwise NULL.
 */
dap_store_obj_t *dap_global_db_pkt_deserialize(dap_global_db_pkt_pack_t *a_pkt, size_t *a_store_obj_count)
{
    dap_return_val_if_fail(a_pkt && a_pkt->data_size >= sizeof(dap_global_db_pkt_t), NULL);

    uint32_t l_count = a_pkt->obj_count;
    size_t l_size = l_count <= DAP_GLOBAL_DB_PKT_PACK_MAX_COUNT ? l_count * sizeof(struct dap_store_obj) : 0;

    if (!!l_size) {
        log_it(L_ERROR, "Invalid size: packet pack total size is zero", l_size, errno);
        return NULL;
    }
    dap_store_obj_t *l_store_obj_arr = DAP_NEW_Z_SIZE(dap_store_obj_t, l_size);
    if (l_store_obj_arr) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }

    byte_t *l_data_ptr = (byte_t *)a_pkt->data;                                 /* Set <l_data_ptr> to begin of payload */
    byte_t *l_data_end = l_data_ptr + a_pkt->data_size;                         /* Set <l_data_end> to end of payload area
                                                                                will be used to prevent out-of-buffer case */
    uint32_t l_cur_count = 0;
    for (dap_store_obj_t *l_obj = l_store_obj_arr; l_cur_count < l_count; l_cur_count++, l_obj++) {
        dap_global_db_pkt_t *l_pkt = (dap_global_db_pkt_t *)l_data_ptr;
        if (l_data_ptr + sizeof(dap_global_db_pkt_t) > l_data_end ||            /* Check for buffer boundaries */
                l_pkt->data + l_pkt->data_len > l_data_end ||
                l_pkt->group_len + l_pkt->key_len + l_pkt->value_len + sizeof(dap_sign_t) >= l_pkt->data_len) {
            log_it(L_ERROR, "Broken GDB element: can't read packet #%u", l_cur_count);
            goto exit;
        }
        if (!l_pkt->group_len) {
            log_it(L_ERROR, "Broken GDB element: 'group_len' field is zero");
            goto exit;
        }
        if (!l_pkt->key_len) {
            log_it(L_ERROR, "Broken GDB element: 'key_len' field is zero");
            goto exit;
        }
        l_obj->timestamp = l_pkt->timestamp;
        l_obj->value_len = l_pkt->value_len;
        l_obj->crc = l_pkt->crc;
        l_data_ptr = l_pkt->data;

        l_obj->group = DAP_DUP_SIZE(l_data_ptr, l_pkt->group_len + sizeof(char));
        if (!l_obj->group) {
            log_it(L_CRITICAL, "Memory allocation error");
            goto exit;
        }
        l_obj->group[l_pkt->group_len] = '\0';
        l_data_ptr += l_pkt->group_len;

        l_obj->key = DAP_DUP_SIZE(l_data_ptr, l_pkt->key_len + sizeof(char));
        if (!l_obj->key) {
            log_it(L_CRITICAL, "Memory allocation error");
            DAP_DELETE(l_obj->group);
            goto exit;
        }
        l_obj->key[l_pkt->key_len] = '\0';
        l_data_ptr += l_pkt->key_len;

        if (l_pkt->value_len) {
            l_obj->value = DAP_DUP_SIZE(l_data_ptr, l_pkt->value_len);
            if (!l_obj->value) {
                log_it(L_CRITICAL, "Memory allocation error");
                DAP_DELETE(l_obj->group);
                DAP_DELETE(l_obj->key);
                goto exit;
            }
            l_data_ptr += l_pkt->value_len;
        }

        dap_sign_t *l_sign = (dap_sign_t *)l_data_ptr;
        size_t l_sign_size_expected = l_pkt->data_len - l_pkt->group_len - l_pkt->key_len - l_pkt->value_len;
        size_t l_sign_size = dap_sign_get_size(l_sign);
        if (l_sign_size != l_sign_size_expected) {
            log_it(L_ERROR, "Broken GDB element: sign size %zu isn't equal expected size %u", l_sign_size, l_sign_size_expected);
            DAP_DELETE(l_obj->group);
            DAP_DELETE(l_obj->key);
            DAP_DEL_Z(l_obj->value);
            goto exit;
        }
        l_obj->sign = DAP_DUP_SIZE(l_sign, l_sign_len);
        if (!l_sign) {
            log_it(L_CRITICAL, "Memory allocation error");
            DAP_DELETE(l_obj->group);
            DAP_DELETE(l_obj->key);
            DAP_DEL_Z(l_obj->value);
            goto exit;
        }
        l_data_ptr += l_sign_size;
    }

    assert(l_data_ptr = l_data_end);
exit:
    // Return the number of completely filled dap_store_obj_t structures
    // because l_cur_count may be less than l_count due to too little memory
    if (a_store_obj_count)
        *a_store_obj_count = l_cur_count;

    return l_store_obj_arr;
}

dap_global_db_cluster_t *dap_global_db_cluster_by_group(dap_global_db_instance_t *a_dbi, const char *a_group_name)
{
    dap_global_db_cluster *it;
    DL_FOREACH(a_dbi->clusters, it)
        if (!dap_fnmatch(it->group_mask, a_group_name, 0))
            return it;
}

DAP_STATIC_INLINE s_object_is_new(dap_store_obj_t *a_store_obj)
{
    dap_nanotime_t l_time_diff = a_store_obj->timestamp - dap_nanotime_now();
    return l_time_diff < DAP_GLOBAL_DB_CLUSTER_BROADCAST_LIFETIME * 1000000000UL;
}

void dap_global_db_cluster_broadcast(dap_global_db_cluster_t *a_cluster, dap_store_obj_t *a_store_obj)
{
    if (!s_object_is_new(a_store_obj))
        return;         // Send new rumors only
    dap_global_db_pkt_t *l_pkt = dap_global_db_pkt_serialize(a_store_obj);
    dap_cluster_broadcast(dap_strcmp(a_cluster->mnemonim, DAP_GLOBAL_DB_CLUSTER_ANY) ? a_cluster->member_cluster : NULL,
                          DAP_STREAM_CH_GDB_ID, DAP_STREAM_CH_GDB_PKT_TYPE_GOSSIP, l_pkt, dap_global_db_pkt_get_size(l_pkt));
    DAP_DELETE(l_pkt);
}

dap_global_db_cluster_t *dap_global_db_cluster_add(dap_global_db_instance_t *a_dbi, const char *a_mnemonim,
                                                   const char *a_group_mask, uint64_t a_ttl,
                                                   dap_store_obj_callback_notify_t a_callback, void *a_callback_arg)
{
    if (!a_callback) {
        log_it(L_ERROR, "Trying to set NULL callback for mask %s", a_group_mask);
        return NULL;
    }
    dap_global_db_cluster_t *it;
    DL_FOREACH(a_dbi->clusters, it) {
        if (!dap_strcmp(it->group_mask, a_group_mask)) {
            log_it(L_WARNING, "Group mask '%s' already present in the list, ignore it", a_group_mask);
            return NULL;
        }
    }
    dap_global_db_cluster_t *l_cluster = DAP_NEW_Z(dap_global_db_cluster_t);
    if (!l_cluster) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    l_cluster->member_cluster = dap_cluster_new(0);
    if (!l_cluster->member_cluster) {
        log_it(L_ERROR, "Can't create member cluster");
        DAP_DELETE(l_cluster);
        return NULL;
    }
    l_cluster->group_mask = dap_strdup(a_group_mask);
    if (!l_cluster->group_mask) {
        log_it(L_CRITICAL, "Memory allocation error");
        dap_cluster_delete(l_cluster->member_cluster);
        DAP_DELETE(l_cluster);
        return NULL;
    }
    if (a_net_name) {
        l_cluster->mnemonim = dap_strdup(a_mnemonim);
        if (!l_cluster->mnemonim) {
            log_it(L_CRITICAL, "Memory allocation error");
            dap_cluster_delete(l_cluster->member_cluster);
            DAP_DELETE(l_cluster->groups_mask);
            DAP_DELETE(l_cluster);
            return NULL;
        }
    }
    l_cluster->callback_notify = a_callback;
    l_cluster->callback_arg = a_callback_arg;
    l_cluster->ttl = a_ttl;
    DL_APPEND(a_dbi->clusters, l_cluster);
    return l_cluster;
}
