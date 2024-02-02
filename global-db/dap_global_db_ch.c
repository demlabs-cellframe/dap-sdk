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

#define LOG_TAG "dap_stream_ch_global_db"

static void s_stream_ch_new(dap_stream_ch_t *a_ch, void *a_arg);
static void s_stream_ch_delete(dap_stream_ch_t *a_ch, void *a_arg);
static void s_stream_ch_packet_in(dap_stream_ch_t *a_ch, void *a_arg);
static void s_stream_ch_packet_out(dap_stream_ch_t *a_ch, void *a_arg);
static void s_stream_ch_io_complete(dap_events_socket_t *a_es, void *a_arg, int a_errno);
static void s_stream_ch_write_error_unsafe(dap_stream_ch_t *a_ch, uint64_t a_net_id, uint64_t a_chain_id, uint64_t a_cell_id, const char * a_err_string);
static void s_gossip_payload_callback(void *a_payload, size_t a_payload_size, dap_stream_node_addr_t a_sender_addr);

/**
 * @brief dap_stream_ch_gdb_init
 * @return
 */
int dap_global_db_ch_init()
{
    log_it(L_NOTICE, "Global DB exchange channel initialized");
    dap_stream_ch_proc_add(DAP_STREAM_CH_GDB_ID, s_stream_ch_new, s_stream_ch_delete,
                                                 s_stream_ch_packet_in, s_stream_ch_packet_out);
    assert(!dap_stream_ch_gossip_callback_add(DAP_STREAM_CH_GDB_ID, s_gossip_payload_callback));
    return 0;
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
    a_ch->stream->esocket->callbacks.write_finished_callback = s_stream_ch_io_complete;
    debug_if(g_dap_global_db_debug_more, L_NOTICE, "Created GDB sync channel %p with internal data %p", a_ch, l_ch_gdb);
}

/**
 * @brief s_stream_ch_delete
 * @param ch
 * @param arg
 */
static void s_stream_ch_delete(dap_stream_ch_t *a_ch, void *a_arg)
{
    UNUSED(a_arg);
    dap_stream_ch_gdb_t *l_ch_gdb = DAP_STREAM_CH_GDB(a_ch);
    debug_if(g_dap_global_db_debug_more, L_NOTICE, "Destroyed GDB sync channel %p with internal data %p", a_ch, l_ch_gdb);
    DAP_DEL_Z(a_ch->internal);
}

static bool s_process_hashes(dap_proc_thread_t UNUSED_ARG *a_thread, void *a_arg)
{
     dap_global_db_hash_pkt_t *l_pkt = (dap_global_db_hash_pkt_t *)((byte_t *)a_arg + sizeof(dap_stream_node_addr_t));
     const char *l_group = (const char *)l_pkt->group_n_hashses;
     dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(dap_global_db_instance_get_default(), l_group);
     if (!l_cluster)
         return false;
     dap_global_db_driver_hash_t *l_hashes = (dap_global_db_driver_hash_t *)(l_group + l_pkt->group_name_len);
     dap_global_db_hash_pkt_t *l_ret = NULL;
     int j = 0;
     for (uint32_t i = 0; i < l_pkt->hashes_count; i++) {
        if (!dap_global_db_driver_is_hash(l_group, *(l_hashes + i))) {
            if (!l_ret)
                l_ret = DAP_NEW_STACK_SIZE(dap_global_db_hash_pkt_t,
                                           sizeof(dap_global_db_hash_pkt_t) +
                                           l_pkt->group_name_len +
                                           sizeof(dap_global_db_driver_hash_t) * l_pkt->hashes_count);
            if (!l_ret) {
                log_it(L_CRITICAL, "Not enough memory");
                return false;
            }
            l_ret->group_name_len = l_pkt->group_name_len;
            dap_global_db_driver_hash_t *l_ret_hashes = (dap_global_db_driver_hash_t *)(l_ret->group_n_hashses + l_ret->group_name_len);
            l_ret_hashes[j++] = l_hashes[i];
        }
     }
     if (l_ret) {
        l_ret->hashes_count = j;
        dap_worker_t *l_worker = NULL;
        dap_events_socket_uuid_t l_es_uuid = dap_stream_find_by_addr((dap_stream_node_addr_t *)a_arg, &l_worker);
        if (l_worker)
            dap_stream_ch_pkt_send_mt(DAP_STREAM_WORKER(l_worker), l_es_uuid, DAP_STREAM_CH_GDB_ID, DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_REQUEST,
                                      l_ret, dap_global_db_hash_pkt_get_size(l_ret));
     }
     return false;
}

static bool s_process_request(dap_proc_thread_t UNUSED_ARG *a_thread, void *a_arg)
{
     dap_global_db_hash_pkt_t *l_pkt = (dap_global_db_hash_pkt_t *)((byte_t *)a_arg + sizeof(dap_stream_node_addr_t));
     const char *l_group = (const char *)l_pkt->group_n_hashses;
     dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(dap_global_db_instance_get_default(), l_group);
     if (!l_cluster)
         return false;
     dap_stream_node_addr_t *l_sender_addr = (dap_stream_node_addr_t *)a_arg;
     if (l_cluster->links_cluster &&
             dap_cluster_member_find_role(l_cluster->links_cluster, l_sender_addr) == DAP_GDB_MEMBER_ROLE_INVALID) {
         const char *l_name = l_cluster->links_cluster->mnemonim ? l_cluster->links_cluster->mnemonim : l_cluster->groups_mask;
         log_it(L_WARNING, "Node with addr " NODE_ADDR_FP_STR "is not a member of cluster %s", NODE_ADDR_FP_ARGS(l_sender_addr), l_name);
     }
     dap_global_db_driver_hash_t *l_hashes = (dap_global_db_driver_hash_t *)(l_group + l_pkt->group_name_len);
     dap_global_db_pkt_pack_t *l_pkt_out = dap_global_db_driver_get_by_hash(l_group, l_hashes, l_pkt->hashes_count);
     if (l_pkt_out) {
        dap_worker_t *l_worker = NULL;
        dap_events_socket_uuid_t l_es_uuid = dap_stream_find_by_addr(l_sender_addr, &l_worker);
        if (l_worker)
            dap_stream_ch_pkt_send_mt(DAP_STREAM_WORKER(l_worker), l_es_uuid, DAP_STREAM_CH_GDB_ID, DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_RECORD_PACK,
                                      l_pkt_out, dap_global_db_pkt_pack_get_size(l_pkt_out));
     }
     return false;
}

struct processing_arg {
    dap_stream_node_addr_t addr;
    uint32_t count;
    dap_store_obj_t *objs;
};

static bool s_process_records(dap_proc_thread_t UNUSED_ARG *a_thread, void *a_arg)
{
    dap_return_val_if_fail(a_arg, false);
    struct processing_arg *l_arg = a_arg;
    for (uint32_t i = 0; i < l_arg->count; i++) {
        dap_store_obj_t *l_obj = l_arg->objs + i;
        if (!dap_global_db_pkt_check_sign_crc(l_obj)) {
            log_it(L_WARNING, "Global DB record packet sign verify or CRC check error for group %s and key %s", l_obj->group, l_obj->key);
            dap_store_obj_free_one(l_obj);
            return false;
        }
        if (g_dap_global_db_debug_more) {
            char l_ts_str[64] = { '\0' };
            dap_time_to_str_rfc822(l_ts_str, sizeof(l_ts_str), dap_nanotime_to_sec(l_obj->timestamp));
            char l_hash_str[DAP_HASH_FAST_STR_SIZE];
            dap_hash_fast_t l_sign_hash;
            if (!l_obj->sign || !dap_sign_get_pkey_hash(l_obj->sign, &l_sign_hash))
                strcpy(l_hash_str, "UNSIGNED");
            else
               dap_hash_fast_to_str(&l_sign_hash, l_hash_str, DAP_HASH_FAST_STR_SIZE);
            log_it(L_DEBUG, "Unpacked object: group=\"%s\" key=\"%s\""
                    " timestamp=\"%s\" value_len=%"DAP_UINT64_FORMAT_U" signer_hash=%s" ,
                    l_obj->group, l_obj->key, l_ts_str, l_obj->value_len, l_hash_str);
        }
        dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(dap_global_db_instance_get_default(), l_obj->group);
        if (!l_cluster)
            return false;
        if (l_cluster->links_cluster &&
                dap_cluster_member_find_role(l_cluster->links_cluster, &l_arg->addr) == DAP_GDB_MEMBER_ROLE_INVALID) {
            const char *l_name = l_cluster->links_cluster->mnemonim ? l_cluster->links_cluster->mnemonim : l_cluster->groups_mask;
            log_it(L_WARNING, "Node with addr " NODE_ADDR_FP_STR "is not a member of cluster %s", NODE_ADDR_FP_ARGS_S(l_arg->addr), l_name);
        }
    }
    dap_global_db_set_raw_sync(l_arg->objs, l_arg->count);
    dap_store_obj_free(l_arg->objs, l_arg->count);
    DAP_DELETE(l_arg);
    return false;
}

static void s_gossip_payload_callback(void *a_payload, size_t a_payload_size, dap_stream_node_addr_t a_sender_addr)
{
    dap_global_db_pkt_t *l_pkt = a_payload;
    dap_store_obj_t *l_obj = dap_global_db_pkt_deserialize(l_pkt, a_payload_size);
    if (!l_obj) {
        log_it(L_WARNING, "Wrong Global DB gossip packet rejected");
        return;
    }
    struct processing_arg *l_arg = DAP_NEW_Z(struct processing_arg);
    l_arg->addr = a_sender_addr;
    l_arg->count = 1;
    l_arg->objs = l_obj;
    dap_proc_thread_callback_add_pri(NULL, s_process_records, l_arg, DAP_GLOBAL_DB_TASK_PRIORITY);
}

static void s_stream_ch_packet_in(dap_stream_ch_t *a_ch, void *a_arg)
{
    dap_stream_ch_gdb_t *l_ch_gdb = DAP_STREAM_CH_GDB(a_ch);
    if (!l_ch_gdb || l_ch_gdb->_inheritor != a_ch) {
        log_it(L_ERROR, "Not valid Global DB channel, returning");
        return;
    }
    dap_stream_ch_pkt_t * l_ch_pkt = (dap_stream_ch_pkt_t *)a_arg;
    switch (l_ch_pkt->hdr.type) {

    case DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_HASHES:
    case DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_REQUEST: {
        if (l_ch_pkt->hdr.type == DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_HASHES &&
                dap_proc_thread_get_avg_queue_size() > DAP_GLOBAL_DB_QUEUE_SIZE_MAX)
            break;
        dap_global_db_hash_pkt_t *l_pkt = (dap_global_db_hash_pkt_t *)l_ch_pkt->data;
        if (l_ch_pkt->hdr.data_size < sizeof(dap_global_db_hash_pkt_t) ||
                l_ch_pkt->hdr.data_size != dap_global_db_hash_pkt_get_size(l_pkt)) {
            log_it(L_WARNING, "Invalid packet size %u", l_ch_pkt->hdr.data_size);
            break;
        }
        if (!l_pkt->hashes_count)
            // Nothnig to process
            break;
        byte_t *l_arg = DAP_NEW_Z_SIZE(byte_t, sizeof(dap_stream_node_addr_t) + l_ch_pkt->hdr.data_size);
        if (!l_arg) {
            log_it(L_CRITICAL, "Not enough memory");
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
        size_t l_objs_count = 0;
        dap_store_obj_t *l_objs = dap_global_db_pkt_pack_deserialize(l_pkt, &l_objs_count);
        if (!l_objs) {
            log_it(L_WARNING, "Wrong Global DB record packet rejected");
            break;
        }
        struct processing_arg *l_arg = DAP_NEW_Z(struct processing_arg);
        l_arg->addr = a_ch->stream->node;
        l_arg->count = l_objs_count;
        l_arg->objs = l_objs;
        dap_proc_thread_callback_add_pri(NULL, s_process_records, l_arg, DAP_GLOBAL_DB_TASK_PRIORITY);
    } break;

    default:
        log_it(L_WARNING, "Unknown global DB packet type %hhu", l_ch_pkt->hdr.type);
        break;
    }
}

static void s_stream_ch_packet_out(dap_stream_ch_t *a_ch, void *a_arg)
{

}

static void s_stream_ch_io_complete(dap_events_socket_t *a_es, void *a_arg, int a_errno)
{

}

/**
 * @brief Sets last synchronized object hash for a remote node.
 *
 * @param a_node_addr a node adress
 * @param a_hash a driver hash for database object
 * @param a_group a group name string
 * @return Returns true if successful, otherwise false.
 */
bool dap_db_set_last_hash_remote(dap_stream_node_addr_t a_node_addr, const char *a_group, dap_global_db_driver_hash_t a_hash)
{
    char *l_key = dap_strdup_printf("%"DAP_UINT64_FORMAT_U"%s", a_node_addr.uint64, a_group);
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
dap_global_db_driver_hash_t dap_db_get_last_hash_remote(dap_stream_node_addr_t a_node_addr, const char *a_group)
{
    char *l_key = dap_strdup_printf("%"DAP_UINT64_FORMAT_U"%s", a_node_addr.uint64, a_group);
    size_t l_ret_len = 0;
    byte_t *l_ret_ptr = dap_global_db_get_sync(DAP_GLOBAL_DB_LOCAL_LAST_HASH, l_key, &l_ret_len, NULL, NULL);
    dap_global_db_driver_hash_t l_ret_hash = l_ret_ptr && l_ret_len == sizeof(dap_global_db_driver_hash_t)
            ? *(dap_global_db_driver_hash_t *)l_ret_ptr
            : c_dap_global_db_driver_hash_blank;
    DAP_DELETE(l_ret_ptr);
    return l_ret_hash;
}
