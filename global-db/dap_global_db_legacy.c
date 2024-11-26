#include <string.h>
#include <stdlib.h>

#include "dap_global_db.h"
#include "dap_global_db_legacy.h"
#include "dap_global_db_cluster.h"
#include "dap_global_db_pkt.h"
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_string.h"
#include "dap_time.h"
#include "dap_hash.h"

#define LOG_TAG "dap_global_db_legacy"

/**
 * @brief Starts a thread that readding a log list
 * @note instead dap_db_log_get_list()
 *
 * @param l_net net for sync
 * @param a_addr a pointer to the structure
 * @param a_flags flags
 * @return Returns a pointer to the log list structure if successful, otherwise NULL pointer.
 */
dap_global_db_legacy_list_t *dap_global_db_legacy_list_start(const char *a_net_name)
{
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "Start loading db list_write...");

    dap_global_db_instance_t *l_dbi = dap_global_db_instance_get_default();
    // Add groups for the selected network only
    dap_cluster_t *l_net_links_cluster = dap_cluster_by_mnemonim(a_net_name);
    dap_list_t *l_groups = NULL;
    for (dap_global_db_cluster_t *it = l_dbi->clusters; it; it = it->next)
        if (it->links_cluster == l_net_links_cluster) // Cluster is related to specified network
            l_groups = dap_list_concat(l_groups, dap_global_db_driver_get_groups_by_mask(it->groups_mask));

    dap_list_t *l_group, *l_tmp;
    // Check for banned/whitelisted groups
    if (l_dbi->whitelist || l_dbi->blacklist) {
        dap_list_t *l_used_list = l_dbi->whitelist ? l_dbi->whitelist : l_dbi->blacklist;
        DL_FOREACH_SAFE(l_groups, l_group, l_tmp) {
            dap_list_t *l_used_el;
            bool l_match = false;
            DL_FOREACH(l_used_list, l_used_el) {
                l_match = dap_global_db_group_match_mask(l_group->data, l_used_el->data);
                if (l_match)
                    break;
            }
            if (l_used_list == l_dbi->whitelist ? !l_match : l_match) {
                DAP_DELETE(l_group->data);
                l_groups = dap_list_delete_link(l_groups, l_group);
            }
        }
    }

    size_t l_items_number = 0;
    DL_FOREACH_SAFE(l_groups, l_group, l_tmp) {
        size_t l_group_size = dap_global_db_driver_count(l_group->data, c_dap_global_db_driver_hash_blank, true);
        if (!l_group_size) {
            log_it(L_WARNING, "[!] Group %s is empty on our side, skip it", (char *)l_group->data);
            DAP_DELETE(l_group->data);
            l_groups = dap_list_delete_link(l_groups, l_group);
            continue;
        }
        l_items_number += l_group_size;
    }

    if (!l_items_number)
        return NULL;

    dap_global_db_legacy_list_t *l_db_legacy_list = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_global_db_legacy_list_t, NULL);
    l_db_legacy_list->groups = l_db_legacy_list->current_group = l_groups;
    l_db_legacy_list->items_rest = l_db_legacy_list->items_number = l_items_number;

    return l_db_legacy_list;
}

dap_list_t *dap_global_db_legacy_list_get_multiple(dap_global_db_legacy_list_t *a_db_legacy_list, size_t a_number_limit)
{
    dap_list_t *ret = NULL;
    size_t l_number_limit = a_number_limit;

    while (a_db_legacy_list->current_group) {
        char *l_group_cur = a_db_legacy_list->current_group->data;
        size_t l_values_count = l_number_limit;
        dap_store_obj_t *l_store_objs = dap_global_db_driver_cond_read(l_group_cur, a_db_legacy_list->current_hash, &l_values_count, true);
        int rc = DAP_GLOBAL_DB_RC_NO_RESULTS;
        if (l_store_objs && l_values_count) {
            a_db_legacy_list->current_hash = dap_global_db_driver_hash_get(l_store_objs + l_values_count - 1);
            if (dap_global_db_driver_hash_is_blank(&a_db_legacy_list->current_hash)) {
                rc = DAP_GLOBAL_DB_RC_SUCCESS;
                l_values_count--;
            } else
                rc = DAP_GLOBAL_DB_RC_PROGRESS;
            if (l_values_count) {
                assert(l_number_limit >= l_values_count);
                l_number_limit -= l_values_count;
                for (size_t i = 0; i < l_values_count; i++) {
                    dap_global_db_pkt_old_t *l_pkt = dap_global_db_pkt_serialize_old(l_store_objs + i);
                    if (!l_pkt) {
                        rc = DAP_GLOBAL_DB_RC_ERROR;
                        break;
                    }
                    dap_list_t *l_list_cur = dap_list_last(ret);
                    ret = dap_list_append(ret, l_pkt);
                    if (dap_list_last(ret) == l_list_cur) {
                        rc = DAP_GLOBAL_DB_RC_ERROR;
                        break;
                    }
                }
            }
            dap_store_obj_free(l_store_objs, l_values_count);
        }
        if (rc == DAP_GLOBAL_DB_RC_ERROR) {
            log_it(L_ERROR, "Can't process all database, internal problems occured");
            dap_list_free_full(ret, NULL);
            return NULL;
        }
        if (rc != DAP_GLOBAL_DB_RC_PROGRESS) {
            // go to next group
            a_db_legacy_list->current_group = dap_list_next(a_db_legacy_list->current_group);
            a_db_legacy_list->current_hash = c_dap_global_db_driver_hash_blank;
        }
        if (!l_number_limit)
            break;
    }

    return ret;
}

/**
 * @brief Deallocates memory of a log list.
 *
 * @param a_db_log_list a pointer to the log list structure
 * @returns (none)
 */
void dap_global_db_legacy_list_delete(dap_global_db_legacy_list_t *a_db_legacy_list)
{
    if (!a_db_legacy_list)
        return;
    if (a_db_legacy_list->groups)
        dap_list_free_full(a_db_legacy_list->groups, NULL);
    DAP_DELETE(a_db_legacy_list);
}

/**
 * @brief Multiples data into a_old_pkt structure from a_new_pkt structure.
 * @param a_old_pkt a pointer to the old object
 * @param a_new_pkt a pointer to the new object
 * @return Returns a pointer to the multiple object
 */
dap_global_db_pkt_old_t *dap_global_db_pkt_pack_old(dap_global_db_pkt_old_t *a_old_pkt, dap_global_db_pkt_old_t *a_new_pkt)
{
    if (!a_new_pkt)
        return a_old_pkt;
    dap_global_db_pkt_old_t *l_old_pkt_new = a_old_pkt
        ? DAP_REALLOC_RET_VAL_IF_FAIL(a_old_pkt, sizeof(dap_global_db_pkt_old_t) + a_old_pkt->data_size + a_new_pkt->data_size, NULL)
        : DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_global_db_pkt_old_t, sizeof(dap_global_db_pkt_old_t) + a_new_pkt->data_size, NULL);
    a_old_pkt = l_old_pkt_new;
    memcpy(a_old_pkt->data + a_old_pkt->data_size, a_new_pkt->data, a_new_pkt->data_size);
    a_old_pkt->data_size += a_new_pkt->data_size;
    ++a_old_pkt->obj_count;
    return a_old_pkt;
}

/**
 * @brief Serializes an object into a packed structure.
 * @param a_store_obj a pointer to the object to be serialized
 * @return Returns a pointer to the packed sructure if successful, otherwise NULL.
 */
dap_global_db_pkt_old_t *dap_global_db_pkt_serialize_old(dap_store_obj_t *a_store_obj)
{
    dap_return_val_if_fail(a_store_obj, NULL);

    size_t l_group_len = dap_strlen(a_store_obj->group);
    size_t l_key_len = dap_strlen(a_store_obj->key);
    size_t l_data_size_out = l_group_len + l_key_len + a_store_obj->value_len + sizeof(uint32_t) + sizeof(uint16_t) * 2 + sizeof(uint64_t) * 3;
    dap_global_db_pkt_old_t *l_pkt = DAP_NEW_SIZE(dap_global_db_pkt_old_t, l_data_size_out + sizeof(dap_global_db_pkt_old_t));
    if (!l_pkt) {
        log_it(L_CRITICAL, "Insufficient memory");
        return NULL;
    }
    /* Fill packet header */
    l_pkt->data_size = l_data_size_out;
    l_pkt->obj_count = 1;
    l_pkt->timestamp = 0;
    /* Put serialized data into the payload part of the packet */
    byte_t *pdata = l_pkt->data;
    uint32_t l_type = dap_store_obj_get_type(a_store_obj);
    pdata = dap_mempcpy(pdata, &l_type, sizeof(uint32_t));
    pdata = dap_mempcpy(pdata, &l_group_len, sizeof(uint16_t));
    pdata = dap_mempcpy(pdata, a_store_obj->group, l_group_len);
    memset(pdata, 0, sizeof(uint64_t));
    pdata += sizeof(uint64_t);
    pdata = dap_mempcpy(pdata, &a_store_obj->timestamp, sizeof(uint64_t));
    pdata = dap_mempcpy(pdata, &l_key_len, sizeof(uint16_t));
    pdata = dap_mempcpy(pdata, a_store_obj->key, l_key_len);
    pdata = dap_mempcpy(pdata, &a_store_obj->value_len, sizeof(uint64_t));
    if (a_store_obj->value && a_store_obj->value_len)
        pdata = dap_mempcpy(pdata, a_store_obj->value, a_store_obj->value_len);

    if ((uint32_t)(pdata - l_pkt->data) != l_data_size_out)
        log_it(L_MSG, "! Inconsistent global_db packet! %zu != %zu", pdata - l_pkt->data, l_data_size_out);
    return l_pkt;
}

/**
 * @brief Deserializes some objects from a packed structure into an array of objects.
 * @param pkt a pointer to the serialized packed structure
 * @param store_obj_count[out] a number of deserialized objects in the array
 * @return Returns a pointer to the first object in the array, if successful; otherwise NULL.
 */
dap_store_obj_t *dap_global_db_pkt_deserialize_old(const dap_global_db_pkt_old_t *a_pkt, size_t *a_store_obj_count)
{
uint32_t l_count;
byte_t *pdata, *pdata_end;
dap_store_obj_t *l_store_obj_arr, *l_obj;

    if(!a_pkt || a_pkt->data_size < sizeof(dap_global_db_pkt_old_t))
        return NULL;

    if ( !(l_store_obj_arr = DAP_NEW_Z_COUNT(dap_store_obj_t, a_pkt->obj_count)) ) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }

    pdata = (byte_t*)a_pkt->data; pdata_end = pdata + a_pkt->data_size;
    l_obj = l_store_obj_arr;

    for (l_count = 0; l_count < a_pkt->obj_count; ++l_count, ++l_obj) {
        if ( pdata + sizeof (uint32_t) > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'type' field"); break;
        }
        if (*(uint32_t *)pdata == DAP_GLOBAL_DB_OPTYPE_DEL)
            l_obj->flags = DAP_GLOBAL_DB_RECORD_DEL;
        pdata += sizeof(uint32_t);

        if ( pdata + sizeof (uint16_t) > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'group_length' field"); break;
        }
        uint16_t l_group_len = *(uint16_t *)pdata; pdata += sizeof(uint16_t);

        if (!l_group_len || pdata + l_group_len > pdata_end) {
            log_it(L_ERROR, "Broken GDB element: can't read 'group' field"); break;
        }
        l_obj->group = DAP_NEW_Z_SIZE(char, l_group_len + 1);
        memcpy(l_obj->group, pdata, l_group_len); pdata += l_group_len;

        if ( pdata + sizeof (uint64_t) > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'id' field");
            DAP_DELETE(l_obj->group); break;
        }
        pdata += sizeof(uint64_t);

        if ( pdata + sizeof (uint64_t) > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'timestamp' field");
            DAP_DELETE(l_obj->group); break;
        }
        memcpy(&l_obj->timestamp, pdata, sizeof(uint64_t)); pdata += sizeof(uint64_t);

        if ( pdata + sizeof (uint16_t) > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'key_length' field");
            DAP_DELETE(l_obj->group); break;
        }
        uint16_t l_key_len = *(uint16_t *)pdata; pdata += sizeof(uint16_t);

        if ( !l_key_len || pdata + l_key_len > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: 'key' field");
            DAP_DELETE(l_obj->group); break;
        }
        l_obj->key = DAP_NEW_Z_SIZE(char, l_key_len + 1);
        memcpy((char*)l_obj->key, pdata, l_key_len); pdata += l_key_len;

        if ( pdata + sizeof (uint64_t) > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'value_length' field");
            DAP_DELETE(l_obj->group);
            DAP_DELETE(l_obj->key); break;
        }
        memcpy(&l_obj->value_len, pdata, sizeof(uint64_t)); pdata += sizeof(uint64_t);

        if (l_obj->value_len && pdata + l_obj->value_len > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'value' field");
            DAP_DELETE(l_obj->group);
            DAP_DELETE(l_obj->key); break;
        }
        l_obj->value = DAP_DUP_SIZE(pdata, l_obj->value_len); pdata += l_obj->value_len;

        l_obj->crc = dap_store_obj_checksum(l_obj);
    }

    if ( pdata < pdata_end ) {
        log_it(L_WARNING, "Unprocessed %zu bytes left in GDB packet", pdata_end - pdata);
        l_store_obj_arr = DAP_REALLOC_COUNT(l_store_obj_arr, l_count);
    }

    if (a_store_obj_count)
        *a_store_obj_count = l_count;

    return l_store_obj_arr;
}
