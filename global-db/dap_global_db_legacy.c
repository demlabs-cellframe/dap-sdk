#include <string.h>
#include <stdlib.h>

#include "dap_global_db.h"
#include "dap_global_db_legacy.h"
#include "dap_global_db_cluster.h"
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
    for (dap_global_db_cluster_t *it = l_dbi->clusters; it; it = it->next)
        if (it->links_cluster == l_net_links_cluster) // Cluster is related to specified network
            l_groups_names = dap_list_concat(l_groups_names, dap_global_db_driver_get_groups_by_mask(it->groups_mask));

    dap_list_t *l_group, *l_tmp;
    // Check for banned/whitelisted groups
    if (l_dbi->whitelist || l_dbi->blacklist) {
        dap_list_t *l_used_list = l_dbi->whitelist ? l_dbi->whitelist : l_dbi->blacklist;
        DL_FOREACH_SAFE(l_groups_names, l_group, l_tmp) {
            dap_list_t *l_used_el;
            bool l_match = false;
            DL_FOREACH(l_used_list, l_used_el) {
                l_match = dap_global_db_group_match_mask(l_group->data, l_used_el->data);
                if (l_match)
                    break;
            }
            if (l_used_list == l_dbi->whitelist ? !l_match : l_match)
                l_groups_names = dap_list_delete_link(l_groups_names, l_group);
        }
    }

    size_t l_items_number = 0;
    DL_FOREACH_SAFE(l_groups, l_group, l_tmp) {
        size_t l_group_size = dap_global_db_driver_count(l_group->data, c_dap_global_db_driver_hash_blank);
        if (!l_group_size) {
            log_it(L_WARNING, "[!] Group %s is empty on our side, skip it", l_group->data);
            l_groups = dap_list_delete_link(l_groups, l_group);
            DAP_DELETE(l_group->data);
            DAP_DELETE(l_group);
            continue;
        }
        l_items_number += l_group_size;
    }

    if (!l_items_number)
        return NULL;

    dap_global_db_legacy_list_t *l_db_legacy_list;
    DAP_NEW_Z_RET_VAL(l_db_legacy_list, dap_global_db_legacy_list_t, NULL);
    l_db_legacy_list->groups = l_groups_names;
    l_db_legacy_list->items_rest = l_db_legacy_list->items_number = l_items_number;

    return l_db_legacy_list;
}

dap_global_db_legacy_list_obj_t *dap_global_db_legacy_list_get_multiple(dap_global_db_legacy_list_t *a_db_log_list, size_t a_number_limit)
{
    dap_list_t *it, *tmp;
    DL_FOREACH_SAFE(a_db_log_list->groups, it, tmp) {
        char *l_group_cur = it->data;
        char l_obj_type = dap_global_db_group_match_mask(l_group->name, "*.del") ? DAP_GLOBAL_DB_OPTYPE_DEL : DAP_GLOBAL_DB_OPTYPE_ADD;
        size_t l_del_name_len = strlen(l_group_cur->name) - 4; //strlen(".del");

        while (l_group_cur->count && l_db_legacy_list->is_process) {
            // Number of records to be synchronized
            size_t l_item_count = 0;//min(64, l_group_cur->count);
            size_t l_objs_total_size = 0;
            dap_store_obj_t *l_objs = dap_global_db_get_all_raw_sync(l_group_cur->name, &l_item_count);
            // go to next group
            if (!l_objs)
                break;
            l_group_cur->count = 0; //-= l_item_count;
            dap_list_t *l_list = NULL;
            for (size_t i = 0; i < l_item_count; i++) {
                dap_store_obj_t *l_obj_cur = l_objs + i;
                if (!l_obj_cur)
                    continue;
                l_obj_cur->type = l_obj_type;
                if (l_obj_type == DAP_GLOBAL_DB_OPTYPE_DEL) {

                    DAP_DELETE((char *)l_obj_cur->group);
                    l_obj_cur->group = dap_strdup(l_del_group_name_replace);
                }
                dap_global_db_legacy_list_obj_t *l_list_obj = DAP_NEW_Z(dap_global_db_legacy_list_obj_t);
                if (!l_list_obj) {
                    log_it(L_CRITICAL, "%s", g_error_memory_alloc);
                    dap_store_obj_free(l_objs, l_item_count);
                    return NULL;
                }
                uint64_t l_cur_id = l_obj_cur->id;
                l_obj_cur->id = 0;
                dap_global_db_pkt_old_t *l_pkt = dap_global_db_pkt_serialize_old(l_obj_cur);
                dap_hash_fast(l_pkt->data, l_pkt->data_size, &l_list_obj->hash);
                dap_global_db_pkt_change_id(l_pkt, l_cur_id);
                l_list_obj->pkt = l_pkt;
                l_list = dap_list_append(l_list, l_list_obj);
                l_objs_total_size += dap_global_db_legacy_list_obj_get_size(l_list_obj);
            }
            dap_store_obj_free(l_objs, l_item_count);

    if (a_msg->total_records)
        l_store_objs = dap_global_db_driver_cond_read(a_msg->group, a_msg->last_hash, &l_values_count);
    int l_rc = DAP_GLOBAL_DB_RC_NO_RESULTS;
    if (l_store_objs && l_values_count) {
        a_msg->processed_records += a_msg->values_page_size;
        a_msg->last_hash = dap_global_db_driver_hash_get(l_store_objs + l_values_count - 1);
        if (dap_global_db_driver_hash_is_blank(&a_msg->last_hash)) {
            l_rc = DAP_GLOBAL_DB_RC_PROGRESS;
            l_values_count--;
        } else
            l_rc = DAP_GLOBAL_DB_RC_SUCCESS;
    }

    bool l_ret = false;
    dap_global_db_hash_pkt_t *l_hashes_pkt = dap_global_db_driver_hashes_read(l_group, l_pkt->last_hash);
    if (l_hashes_pkt && l_hashes_pkt->hashes_count) {
        dap_global_db_driver_hash_t *l_hashes_diff = (dap_global_db_driver_hash_t *)(l_hashes_pkt->group_n_hashses + l_hashes_pkt->group_name_len);
        uint64_t l_time_store_lim_sec = l_cluster->ttl ? l_cluster->ttl : l_cluster->dbi->store_time_limit * 3600ULL;
        uint64_t l_limit_time = l_time_store_lim_sec ? dap_nanotime_now() - dap_nanotime_from_sec(l_time_store_lim_sec) : 0;
        if (l_limit_time) {
            uint32_t i;
            for (i = 0; i < l_hashes_pkt->hashes_count && be64toh((l_hashes_diff + i)->bets) < l_limit_time; i++) {
                if (dap_global_db_driver_hash_is_blank(l_hashes_diff + i))
                    break;
                dap_store_obj_t l_to_del = { .timestamp = be64toh((l_hashes_diff + i)->bets),
                                             .crc = be64toh((l_hashes_diff + i)->becrc),
                                             .group = (char *)l_group };
                int l_res = dap_global_db_driver_delete(&l_to_del, 1);
                if (g_dap_global_db_debug_more) {
                    char l_to_del_ts[DAP_TIME_STR_SIZE];
                    dap_time_to_str_rfc822(l_to_del_ts, sizeof(l_to_del_ts), dap_nanotime_to_sec(l_to_del.timestamp));
                    log_it(l_res ? L_WARNING : L_DEBUG, "%s too old object with group %s and timestamp %s",
                                                             l_res ? "Can't remove" : "Removed", l_group, l_to_del_ts);
                }
            }
            if (i == l_hashes_pkt->hashes_count) {
                l_pkt->last_hash = l_hashes_diff[l_hashes_pkt->hashes_count - 1];
                DAP_DELETE(l_hashes_pkt);
                return true;
            }
            if (i) {
                l_hashes_pkt->hashes_count -= i;
                memmove(l_hashes_diff, l_hashes_diff + i, sizeof(dap_global_db_driver_hash_t) * l_hashes_pkt->hashes_count);
            }
        }
        l_pkt->last_hash = l_hashes_diff[l_hashes_pkt->hashes_count - 1];
        l_ret = !dap_global_db_driver_hash_is_blank(&l_pkt->last_hash);
        if (!l_ret) {
            --l_hashes_pkt->hashes_count;
            //dap_db_set_last_hash_remote(l_req->link, l_req->group, l_hashes_diff[l_hashes_pkt->hashes_count - 1]);
        }
        if (l_hashes_pkt->hashes_count) {
            debug_if(g_dap_global_db_debug_more, L_INFO, "OUT: GLOBAL_DB_HASHES packet for group %s with records count %u",
                                                                                                    l_group, l_hashes_pkt->hashes_count);
            dap_stream_ch_pkt_send_by_addr(l_sender_addr,
                                           DAP_STREAM_CH_GDB_ID, DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_HASHES,
                                           l_hashes_pkt, dap_global_db_hash_pkt_get_size(l_hashes_pkt));
        }
        DAP_DELETE(l_hashes_pkt);

    if (!a_db_log_list || !a_count)
        return NULL;
    pthread_mutex_lock(&a_db_log_list->list_mutex);
    if (!a_db_log_list->is_process) {
        // Log list was not deleted, but caching thread is finished, so no need to lock anymore
        pthread_mutex_unlock(&a_db_log_list->list_mutex);
    }
    size_t l_count = a_db_log_list->items_list
            ? *a_count
              ? dap_min(*a_count, dap_list_length(a_db_log_list->items_list))
              : dap_list_length(a_db_log_list->items_list)
            : 0;
    size_t l_old_size = a_db_log_list->size, l_out_size = 0;
    dap_global_db_legacy_list_obj_t **l_ret = DAP_NEW_Z_COUNT(dap_global_db_legacy_list_obj_t*, l_count);
    if (l_ret) {
        *a_count = l_count;
        dap_list_t *l_elem, *l_tmp;
        DL_FOREACH_SAFE(a_db_log_list->items_list, l_elem, l_tmp) {
            l_out_size += dap_global_db_legacy_list_obj_get_size(l_elem->data);
            if (a_size_limit && l_out_size > a_size_limit)
                break;
            l_ret[*a_count - l_count] = l_elem->data;
            --a_db_log_list->items_rest;
            a_db_log_list->size -= dap_global_db_legacy_list_obj_get_size(l_elem->data);
            a_db_log_list->items_list = dap_list_delete_link(a_db_log_list->items_list, l_elem);
            if (!(--l_count))
                break;
        }
        if (l_count) {
            *a_count -= l_count;
            l_ret = DAP_REALLOC_COUNT(l_ret, *a_count);
        }
        log_it(L_MSG, "[!] Extracted %zu records from log_list (size %zu), left %zu", *a_count, l_out_size, l_count);
        if (l_old_size > DAP_DB_LOG_LIST_MAX_SIZE && a_db_log_list->size <= DAP_DB_LOG_LIST_MAX_SIZE && a_db_log_list->is_process)
            pthread_cond_signal(&a_db_log_list->cond);
    }
    if (a_db_log_list->is_process) {
        pthread_mutex_unlock(&a_db_log_list->list_mutex);
        if (!l_ret)
            l_ret = DAP_INT_TO_POINTER(0x1); // Thread is not yet done...
    }
    return l_ret;
}


/**
 * @brief Deallocates memory of a list item
 *
 * @param a_item a pointer to the list item
 * @returns (none)
 */
static void s_dap_global_db_legacy_list_delete_item(void *a_item)
{
    dap_global_db_legacy_list_obj_t *l_list_item = (dap_global_db_legacy_list_obj_t *)a_item;
    DAP_DELETE(l_list_item->pkt);
    DAP_DELETE(l_list_item);
}

/**
 * @brief Deallocates memory of a log list.
 *
 * @param a_db_log_list a pointer to the log list structure
 * @returns (none)
 */
void dap_global_db_legacy_list_delete(dap_global_db_legacy_list_t *a_db_log_list)
{
    if(!a_db_log_list)
        return;
    // stop thread if it has created
    if(a_db_log_list->thread) {
        pthread_mutex_lock(&a_db_log_list->list_mutex);
        a_db_log_list->is_process = false;
        pthread_cond_signal(&a_db_log_list->cond);
        pthread_mutex_unlock(&a_db_log_list->list_mutex);
        pthread_join(a_db_log_list->thread, NULL);
    }
    dap_list_free_full(a_db_log_list->items_list, (dap_callback_destroyed_t)s_dap_global_db_legacy_list_delete_item);
    pthread_mutex_destroy(&a_db_log_list->list_mutex);
    dap_list_free_full(a_db_log_list->groups, NULL);
    DAP_DELETE(a_db_log_list);
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
    a_old_pkt = a_old_pkt
            ? DAP_REALLOC(a_old_pkt, sizeof(dap_global_db_pkt_old_t) + a_old_pkt->data_size + a_new_pkt->data_size)
            : DAP_NEW_Z_SIZE(dap_global_db_pkt_old_t, sizeof(dap_global_db_pkt_old_t) + a_new_pkt->data_size);
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
    byte_t *pdata;

    if (!a_store_obj)
        return NULL;

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
    l_pkt->data_size = l_data_size_out;
    l_pkt->obj_count = 1;
    l_pkt->timestamp = 0;
    /* Put serialized data into the payload part of the packet */
    pdata = l_pkt->data;
    memcpy(pdata,   &a_store_obj->type,     sizeof(uint32_t));      pdata += sizeof(uint32_t);
    memcpy(pdata,   &l_group_len,           sizeof(uint16_t));      pdata += sizeof(uint16_t);
    memcpy(pdata,   a_store_obj->group,     l_group_len);           pdata += l_group_len;
    memset(pdata,   0,                      sizeof(uint64_t));      pdata += sizeof(uint64_t);
    memcpy(pdata,   &a_store_obj->timestamp,sizeof(uint64_t));      pdata += sizeof(uint64_t);
    memcpy(pdata,   &l_key_len,             sizeof(uint16_t));      pdata += sizeof(uint16_t);
    memcpy(pdata,   a_store_obj->key,       l_key_len);             pdata += l_key_len;
    memcpy(pdata,   &a_store_obj->value_len,sizeof(uint64_t));      pdata += sizeof(uint64_t);
    memcpy(pdata,   a_store_obj->value,     a_store_obj->value_len);pdata += a_store_obj->value_len;
    if ((uint32_t)(pdata - l_pkt->data) != l_data_size_out) {
        log_it(L_MSG, "! Inconsistent global_db packet! %u != %u", (uint32_t)(pdata - l_pkt->data), l_data_size_out);
    }
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
        log_it(L_CRITICAL, "%s", g_error_memory_alloc);
        return NULL;
    }

    pdata = (byte_t*)a_pkt->data; pdata_end = pdata + a_pkt->data_size;
    l_obj = l_store_obj_arr;

    for (l_count = 0; l_count < a_pkt->obj_count; ++l_count, ++l_obj) {
        if ( pdata + sizeof (uint32_t) > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'type' field"); break;
        }
        memcpy(&l_obj->type, pdata, sizeof(uint32_t)); pdata += sizeof(uint32_t);

        if ( pdata + sizeof (uint16_t) > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: can't read 'group_length' field"); break;
        }
        memcpy(&l_obj->group_len, pdata, sizeof(uint16_t)); pdata += sizeof(uint16_t);

        if (!l_obj->group_len || pdata + l_obj->group_len > pdata_end) {
            log_it(L_ERROR, "Broken GDB element: can't read 'group' field"); break;
        }
        l_obj->group = DAP_NEW_Z_SIZE(char, l_obj->group_len + 1);
        memcpy(l_obj->group, pdata, l_obj->group_len); pdata += l_obj->group_len;

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
        memcpy(&l_obj->key_len, pdata, sizeof(uint16_t)); pdata += sizeof(uint16_t);

        if ( !l_obj->key_len || pdata + l_obj->key_len > pdata_end ) {
            log_it(L_ERROR, "Broken GDB element: 'key' field");
            DAP_DELETE(l_obj->group); break;
        }
        l_obj->key = DAP_NEW_Z_SIZE(char, l_obj->key_len + 1);
        memcpy((char*)l_obj->key, pdata, l_obj->key_len); pdata += l_obj->key_len;

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

        l_obj->crc = dap_store_obj_checksum(); // TODO
    }

    if ( pdata < pdata_end ) {
        log_it(L_WARNING, "Unprocessed %zu bytes left in GDB packet", pdata_end - pdata);
        l_store_obj_arr = DAP_REALLOC_COUNT(l_store_obj_arr, l_count);
    }

    if (a_store_obj_count)
        *a_store_obj_count = l_count;

    return l_store_obj_arr;
}
