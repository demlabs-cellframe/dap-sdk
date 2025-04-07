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
#include "dap_strfuncs.h"
#include "dap_global_db.h"
#include "dap_global_db_ch.h"
#include "dap_global_db_pkt.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_ch_gossip.h"
#include "dap_global_db_cluster.h"

#define LOG_TAG "dap_global_db_ch"

static void s_stream_ch_new(dap_stream_ch_t *a_ch, void *a_arg);
static void s_stream_ch_delete(dap_stream_ch_t *a_ch, void *a_arg);
static bool s_stream_ch_packet_in(dap_stream_ch_t *a_ch, void *a_arg);
static void s_gossip_payload_callback(void *a_payload, size_t a_payload_size, dap_stream_node_addr_t a_sender_addr);

/**
 * @brief dap_stream_ch_gdb_init
 * @return
 */
int dap_global_db_ch_init()
{
    log_it(L_NOTICE, "Global DB exchange channel initialized");
    dap_stream_ch_proc_add(DAP_STREAM_CH_GDB_ID, s_stream_ch_new, s_stream_ch_delete, s_stream_ch_packet_in, NULL);
    return dap_stream_ch_gossip_callback_add(DAP_STREAM_CH_GDB_ID, s_gossip_payload_callback);
}

void dap_global_db_ch_deinit()
{

}

/**
 * @brief s_stream_ch_new
 * @param a_ch
 * @param arg
 */
void s_stream_ch_new(dap_stream_ch_t *a_ch, void *a_arg)
{
    UNUSED(a_arg);
    a_ch->internal = DAP_NEW_Z(dap_stream_ch_gdb_t);
    dap_stream_ch_gdb_t *l_ch_gdb = DAP_STREAM_CH_GDB(a_ch);
    if (!l_ch_gdb) {
        log_it(L_CRITICAL, "Insufficient memory!");
        return;
    }
    l_ch_gdb->_inheritor = a_ch;
    debug_if(g_dap_global_db_debug_more, L_NOTICE, "Created GDB sync channel %p with internal data %p", a_ch, l_ch_gdb);
}

/**
 * @brief s_stream_ch_delete
 * @param ch
 * @param arg
 */
static void s_stream_ch_delete(dap_stream_ch_t *a_ch, void UNUSED_ARG *a_arg)
{
    dap_stream_ch_gdb_t *l_ch_gdb = DAP_STREAM_CH_GDB(a_ch);
    debug_if(g_dap_global_db_debug_more, L_NOTICE, "Destroyed GDB sync channel %p with internal data %p", a_ch, l_ch_gdb);
    DAP_DEL_Z(a_ch->internal);
}

bool s_proc_thread_reader(void *a_arg)
{
    dap_global_db_start_pkt_t *l_pkt = (dap_global_db_start_pkt_t *)((byte_t *)a_arg + sizeof(dap_stream_node_addr_t) + sizeof(byte_t));
    byte_t l_type = *((byte_t *)a_arg + sizeof(dap_stream_node_addr_t));
    const char *l_group = (const char *)l_pkt->group;
    dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(dap_global_db_instance_get_default(), l_group);
    if (!l_cluster) {
        log_it(L_ERROR, "Cluster for group %s not found", l_group);
        return false;
    }
    dap_stream_node_addr_t *l_sender_addr = (dap_stream_node_addr_t *)a_arg;
    if (dap_cluster_member_find_role(l_cluster->links_cluster, l_sender_addr) == DAP_GDB_MEMBER_ROLE_INVALID) {
        const char *l_name = l_cluster->links_cluster->mnemonim ? l_cluster->links_cluster->mnemonim : l_cluster->groups_mask;
        log_it(L_WARNING, "Node with addr " NODE_ADDR_FP_STR " is not a member of cluster %s", NODE_ADDR_FP_ARGS(l_sender_addr), l_name);
        return false;
    }
    bool l_ret = false;
    dap_global_db_hash_pkt_t *l_hashes_pkt = dap_global_db_driver_hashes_read(l_group, l_pkt->last_hash);
    if (l_hashes_pkt && l_hashes_pkt->hashes_count) {
        dap_global_db_driver_hash_t *l_hashes_diff = (dap_global_db_driver_hash_t *)(l_hashes_pkt->group_n_hashses + l_hashes_pkt->group_name_len);
        dap_nanotime_t l_ttl = dap_nanotime_from_sec(l_cluster->ttl);
        if (l_ttl) {
            dap_nanotime_t l_now = dap_nanotime_now();
            uint32_t i;
            for (i = 0; i < l_hashes_pkt->hashes_count && be64toh((l_hashes_diff + i)->bets) + l_ttl < l_now; i++) {
                if (dap_global_db_driver_hash_is_blank(l_hashes_diff + i))
                    break;
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
    } else if (l_type != DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_GROUP_REQUEST) {
        debug_if(g_dap_global_db_debug_more, L_INFO, "OUT: GLOBAL_DB_GROUP_REQUEST packet for group %s from first record", l_group);
        dap_global_db_driver_hash_t l_tmp_hash = l_pkt->last_hash;
        l_pkt->last_hash = c_dap_global_db_driver_hash_blank;
        dap_stream_ch_pkt_send_by_addr(l_sender_addr, DAP_STREAM_CH_GDB_ID, DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_GROUP_REQUEST,
                                       l_pkt, dap_global_db_start_pkt_get_size(l_pkt));
        l_pkt->last_hash = l_tmp_hash;
    }
    if (!l_ret)
        DAP_DELETE(a_arg);
    return l_ret;
}

static bool s_process_hashes(void *a_arg)
{
    dap_global_db_hash_pkt_t *l_pkt = (dap_global_db_hash_pkt_t *)((byte_t *)a_arg + sizeof(dap_stream_node_addr_t));
    const char *l_group = (const char *)l_pkt->group_n_hashses;
    dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(dap_global_db_instance_get_default(), l_group);
    if (!l_cluster) {
        log_it(L_ERROR, "Cluster for group %s not found", l_group);
        DAP_DELETE(a_arg);
        return false;
    }
    dap_global_db_driver_hash_t *l_hashes = (dap_global_db_driver_hash_t *)(l_group + l_pkt->group_name_len);
    uint32_t j = 0;
    for (uint32_t i = 0; i < l_pkt->hashes_count; i++) {
        dap_global_db_driver_hash_t *l_hash_cur = l_hashes + i;
        if (!dap_global_db_driver_is_hash(l_group, *l_hash_cur)) {
            if (i != j)
                *(l_hashes + j) = *l_hash_cur;
            j++;
        }
    }
    l_pkt->hashes_count = j;
    if (l_pkt->hashes_count) {
        debug_if(g_dap_global_db_debug_more, L_INFO, "OUT: GLOBAL_DB_REQUEST packet for group %s with records count %u",
                                                                                        l_group, l_pkt->hashes_count);
        dap_stream_ch_pkt_send_by_addr((dap_stream_node_addr_t *)a_arg,
                                   DAP_STREAM_CH_GDB_ID, DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_REQUEST,
                                   l_pkt, dap_global_db_hash_pkt_get_size(l_pkt));
    }
    DAP_DELETE(a_arg);
    return false;
}

static bool s_process_request(void *a_arg)
{
    dap_global_db_hash_pkt_t *l_pkt = (dap_global_db_hash_pkt_t *)((byte_t *)a_arg + sizeof(dap_stream_node_addr_t));
    const char *l_group = (const char *)l_pkt->group_n_hashses;
    dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(dap_global_db_instance_get_default(), l_group);
    if (!l_cluster) {
        log_it(L_ERROR, "Cluster for group %s not found", l_group);
        DAP_DELETE(a_arg);
        return false;
    }
    dap_stream_node_addr_t *l_sender_addr = (dap_stream_node_addr_t *)a_arg;
    if (dap_cluster_member_find_role(l_cluster->links_cluster, l_sender_addr) == DAP_GDB_MEMBER_ROLE_INVALID) {
        const char *l_name = l_cluster->links_cluster->mnemonim ? l_cluster->links_cluster->mnemonim : l_cluster->groups_mask;
        log_it(L_WARNING, "Node with addr " NODE_ADDR_FP_STR " is not a member of cluster %s", NODE_ADDR_FP_ARGS(l_sender_addr), l_name);
        DAP_DELETE(a_arg);
        return false;
    }
    dap_global_db_driver_hash_t *l_hashes = (dap_global_db_driver_hash_t *)(l_group + l_pkt->group_name_len);
    dap_global_db_pkt_pack_t *l_pkt_out = dap_global_db_driver_get_by_hash(l_group, l_hashes, l_pkt->hashes_count);

    if (l_pkt_out) {
        debug_if(g_dap_global_db_debug_more, L_INFO, "OUT: GLOBAL_DB_RECORD_PACK packet for group %s with records count %u",
                                                                                                l_group, l_pkt_out->obj_count);
        dap_stream_ch_pkt_send_by_addr(l_sender_addr, DAP_STREAM_CH_GDB_ID, DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_RECORD_PACK,
                                       l_pkt_out, dap_global_db_pkt_pack_get_size(l_pkt_out));
        DAP_DELETE(l_pkt_out);
    }
    DAP_DELETE(a_arg);
    return false;
}

bool dap_global_db_ch_check_store_obj(dap_store_obj_t *a_obj, dap_stream_node_addr_t *a_addr)
{
    if (!dap_global_db_pkt_check_sign_crc(a_obj)) {
        log_it(L_WARNING, "Global DB record packet sign verify or CRC check error for group %s and key %s", a_obj->group, a_obj->key);
        return false;
    }
    if (g_dap_global_db_debug_more) {
        char l_ts_str[DAP_TIME_STR_SIZE] = { '\0' };
        dap_time_to_str_rfc822(l_ts_str, sizeof(l_ts_str), dap_nanotime_to_sec(a_obj->timestamp));
        dap_stream_node_addr_t l_signer_addr;
        dap_hash_fast_t l_sign_hash;
        if (a_obj->sign && dap_sign_get_pkey_hash(a_obj->sign, &l_sign_hash))
           dap_stream_node_addr_from_hash(&l_sign_hash, &l_signer_addr);
        log_it(L_DEBUG, "Unpacked object: type='%c', group=\"%s\" key=\"%s\""
                " timestamp=\"%s\" value_len=%zu signer_addr=%s",
                    dap_store_obj_get_type(a_obj),
                        a_obj->group, a_obj->key, l_ts_str, a_obj->value_len,
                            a_obj->sign ? dap_stream_node_addr_to_str_static(l_signer_addr) : "UNSIGNED");
    }
    dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(dap_global_db_instance_get_default(), a_obj->group);
    if (!l_cluster) {
        log_it(L_ERROR, "Cluster for group %s not found", a_obj->group);
        return false;
    }
    if (dap_stream_node_addr_is_blank(a_addr) &&
            l_cluster->links_cluster->type == DAP_CLUSTER_TYPE_EMBEDDED &&
            l_cluster->links_cluster->status == DAP_CLUSTER_STATUS_ENABLED)
        // Unverified stream, let it access to embedded (network) clusters for legacy support
        return true;
    if (!dap_cluster_member_find_unsafe(l_cluster->links_cluster, a_addr)) {
        const char *l_name = l_cluster->links_cluster->mnemonim ? l_cluster->links_cluster->mnemonim : l_cluster->groups_mask;
        log_it(L_WARNING, "Node with addr " NODE_ADDR_FP_STR " is not a member of cluster %s", NODE_ADDR_FP_ARGS(a_addr), l_name);
        return false;
    }
    return true;
}

#ifdef DAP_GLOBAL_DB_WRITE_SERIALIZED
struct processing_arg {
    uint32_t count;
    dap_store_obj_t *objs;
    dap_stream_node_addr_t addr;
};

static bool s_process_records(void *a_arg)
{
    dap_return_val_if_fail(a_arg, false);
    struct processing_arg *l_arg = a_arg;
    bool l_success = false;
    for (uint32_t i = 0; i < l_arg->count; i++)
        if (!(l_success = dap_global_db_ch_check_store_obj(l_arg->objs + i, &l_arg->addr)))
            break;
    if (l_success)
        dap_global_db_set_raw_sync(l_arg->objs, l_arg->count);
    dap_store_obj_free(l_arg->objs, l_arg->count);
    DAP_DELETE(l_arg);
    return false;
}
#endif

static bool s_process_record(void *a_arg)
{
    dap_return_val_if_fail(a_arg, false);
    dap_store_obj_t *l_obj = a_arg;
    if (dap_global_db_ch_check_store_obj(l_obj, (dap_stream_node_addr_t *)l_obj->ext))
        dap_global_db_set_raw_sync(l_obj, 1);
    dap_store_obj_free_one(l_obj);
    return false;
}

static void s_gossip_payload_callback(void *a_payload, size_t a_payload_size, dap_stream_node_addr_t a_sender_addr)
{
    dap_global_db_pkt_t *l_pkt = a_payload;
    dap_store_obj_t *l_obj = dap_global_db_pkt_deserialize(l_pkt, a_payload_size, &a_sender_addr);
    if (!l_obj) {
        log_it(L_WARNING, "Wrong Global DB gossip packet rejected");
        return;
    }
    debug_if(g_dap_global_db_debug_more, L_INFO, "IN: GLOBAL_DB_GOSSIP packet for group %s with key %s",
                 l_obj->group, l_obj->key);
    dap_proc_thread_callback_add_pri(NULL, s_process_record, l_obj, DAP_GLOBAL_DB_TASK_PRIORITY);
}

static bool s_stream_ch_packet_in(dap_stream_ch_t *a_ch, void *a_arg)
{
    dap_stream_ch_gdb_t *l_ch_gdb = DAP_STREAM_CH_GDB(a_ch);
    if (!l_ch_gdb || l_ch_gdb->_inheritor != a_ch) {
        log_it(L_ERROR, "Not valid Global DB channel, returning");
        return false;
    }
    dap_stream_ch_pkt_t * l_ch_pkt = (dap_stream_ch_pkt_t *)a_arg;
    switch (l_ch_pkt->hdr.type) {

    case DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_START:
    case DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_GROUP_REQUEST: {
        dap_global_db_start_pkt_t *l_pkt = (dap_global_db_start_pkt_t *)l_ch_pkt->data;
        if (l_ch_pkt->hdr.data_size < sizeof(dap_global_db_start_pkt_t) ||
                l_ch_pkt->hdr.data_size != dap_global_db_start_pkt_get_size(l_pkt)) {
            log_it(L_WARNING, "Invalid packet size %u", l_ch_pkt->hdr.data_size);
            return false;
        }
        debug_if(g_dap_global_db_debug_more, L_INFO, "IN: %s packet for group %s",
                            l_ch_pkt->hdr.type == DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_START
                            ? "GLOBAL_DB_SYNC_START" : "GLOBAL_DB_GROUP_REQUEST", l_pkt->group);
        byte_t *l_arg = DAP_NEW_Z_SIZE(byte_t, sizeof(dap_stream_node_addr_t) + sizeof(byte_t) + l_ch_pkt->hdr.data_size);
        if (!l_arg) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            break;
        }
        memcpy(l_arg + sizeof(dap_stream_node_addr_t) + sizeof(byte_t), l_pkt, l_ch_pkt->hdr.data_size);
        *(dap_stream_node_addr_t *)l_arg = a_ch->stream->node;
        *(l_arg + sizeof(dap_stream_node_addr_t)) = l_ch_pkt->hdr.type;
        dap_proc_thread_callback_add_pri(NULL, s_proc_thread_reader, l_arg, DAP_GLOBAL_DB_TASK_PRIORITY);
    } break;

    case DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_HASHES:
    case DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_REQUEST: {
        dap_global_db_hash_pkt_t *l_pkt = (dap_global_db_hash_pkt_t *)l_ch_pkt->data;
        if (l_ch_pkt->hdr.data_size < sizeof(dap_global_db_hash_pkt_t) ||
                l_ch_pkt->hdr.data_size != dap_global_db_hash_pkt_get_size(l_pkt)) {
            log_it(L_WARNING, "Invalid packet size %u", l_ch_pkt->hdr.data_size);
            return false;
        }
        if (l_ch_pkt->hdr.type == DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_HASHES &&
                dap_proc_thread_get_avg_queue_size() > DAP_GLOBAL_DB_QUEUE_SIZE_MAX)
            break;
        debug_if(g_dap_global_db_debug_more, L_INFO, "IN: %s packet for group %s with hashes count %u",
                                                l_ch_pkt->hdr.type == DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_HASHES
                                                ? "GLOBAL_DB_HASHES" : "GLOBAL_DB_REQUEST",
                                                l_pkt->group_n_hashses, l_pkt->hashes_count);
        if (!l_pkt->hashes_count)
            // Nothnig to process
            break;
        byte_t *l_arg = DAP_NEW_Z_SIZE(byte_t, sizeof(dap_stream_node_addr_t) + l_ch_pkt->hdr.data_size);
        if (!l_arg) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            break;
        }
        memcpy(l_arg + sizeof(dap_stream_node_addr_t), l_pkt, l_ch_pkt->hdr.data_size);
        *(dap_stream_node_addr_t *)l_arg = a_ch->stream->node;
        dap_proc_queue_callback_t l_callback = l_ch_pkt->hdr.type == DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_HASHES ?
                    s_process_hashes : s_process_request;
        dap_proc_thread_callback_add_pri(NULL, l_callback, l_arg, DAP_GLOBAL_DB_TASK_PRIORITY);
    } break;

    case DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_RECORD_PACK: {
        dap_global_db_pkt_pack_t *l_pkt = (dap_global_db_pkt_pack_t *)l_ch_pkt->data;
        if (l_ch_pkt->hdr.data_size < sizeof(dap_global_db_pkt_pack_t) ||
                l_ch_pkt->hdr.data_size != dap_global_db_pkt_pack_get_size(l_pkt)) {
            log_it(L_WARNING, "Invalid packet size %u", l_ch_pkt->hdr.data_size);
            return false;
        }
        size_t l_objs_count = 0;
#ifdef DAP_GLOBAL_DB_WRITE_SERIALIZED
        dap_store_obj_t *l_objs = dap_global_db_pkt_pack_deserialize(l_pkt, &l_objs_count);
#else
        dap_store_obj_t **l_objs = dap_global_db_pkt_pack_deserialize(l_pkt, &l_objs_count, &a_ch->stream->node);
#endif
        if (!l_objs) {
            log_it(L_WARNING, "Wrong Global DB record packet rejected");
            return false;
        }
        debug_if(g_dap_global_db_debug_more, L_INFO, "IN: GLOBAL_DB_RECORD_PACK packet for group %s with records count %zu",
                                                                                                l_objs->group, l_objs_count);
#ifdef DAP_GLOBAL_DB_WRITE_SERIALIZED
        struct processing_arg *l_arg = DAP_NEW_Z(struct processing_arg);
        *l_arg = (struct processing_arg) { .count = l_objs_count, .objs = l_objs, .addr = a_ch->stream->node };
        dap_proc_thread_callback_add_pri(NULL, s_process_records, l_arg, DAP_GLOBAL_DB_TASK_PRIORITY);
#else
        for (uint32_t i = 0; i < l_objs_count; i++)
            dap_proc_thread_callback_add_pri(NULL, s_process_record, l_objs[i], DAP_GLOBAL_DB_TASK_PRIORITY);
        DAP_DELETE(l_objs);
#endif
    } break;

    default:
        log_it(L_WARNING, "Unknown global DB packet type %hhu", l_ch_pkt->hdr.type);
        return false;
    }
    return true;
}

/**
 * @brief Sets last synchronized object hash for a remote node.
 *
 * @param a_node_addr a node adress
 * @param a_hash a driver hash for database object
 * @param a_group a group name string
 * @return Returns true if successful, otherwise false.
 */
bool dap_global_db_ch_set_last_hash_remote(dap_stream_node_addr_t a_node_addr, const char *a_group, dap_global_db_driver_hash_t a_hash)
{
    char *l_key = dap_strdup_printf("%"DAP_UINT64_FORMAT_U"%s", a_node_addr.uint64, a_group);
    if (!l_key)
        return false;
    bool l_ret = dap_global_db_set(DAP_GLOBAL_DB_LOCAL_LAST_HASH, l_key, &a_hash, sizeof(dap_global_db_driver_hash_t), false, NULL, NULL) == 0;
    DAP_DELETE(l_key);
    return l_ret;
}

/**
 * @brief Gets last id of a remote node.
 *
 * @param a_node_addr a node adress
 * @param a_group a group name string
 * @return Returns last synchronize object driver hash for provided node if successful, otherwise blank hash.
 */
dap_global_db_driver_hash_t dap_global_db_ch_get_last_hash_remote(dap_stream_node_addr_t a_node_addr, const char *a_group)
{
    char *l_key = dap_strdup_printf("%"DAP_UINT64_FORMAT_U"%s", a_node_addr.uint64, a_group);
    if (!l_key)
        return c_dap_global_db_driver_hash_blank;
    size_t l_ret_len = 0;
    byte_t *l_ret_ptr = dap_global_db_get_sync(DAP_GLOBAL_DB_LOCAL_LAST_HASH, l_key, &l_ret_len, NULL, NULL);
    dap_global_db_driver_hash_t l_ret_hash = l_ret_ptr && l_ret_len == sizeof(dap_global_db_driver_hash_t)
            ? *(dap_global_db_driver_hash_t *)l_ret_ptr
            : c_dap_global_db_driver_hash_blank;
    DAP_DEL_Z(l_ret_ptr);
    return l_ret_hash;
}
