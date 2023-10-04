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
#include "dap_global_db_cluster.h"

#define LOG_TAG "dap_stream_ch_global_db"

static void s_stream_ch_new(dap_stream_ch_t *a_ch, void *a_arg);
static void s_stream_ch_delete(dap_stream_ch_t *a_ch, void *a_arg);
static void s_stream_ch_packet_in(dap_stream_ch_t *a_ch, void *a_arg);
static void s_stream_ch_packet_out(dap_stream_ch_t *a_ch, void *a_arg);
static void s_stream_ch_io_complete(dap_events_socket_t *a_es, void *a_arg, int a_errno);
static void s_stream_ch_write_error_unsafe(dap_stream_ch_t *a_ch, uint64_t a_net_id, uint64_t a_chain_id, uint64_t a_cell_id, const char * a_err_string);

static void s_ch_gdb_go_idle(dap_stream_ch_gdb_t *a_ch_gdb);

/**
 * @brief dap_stream_ch_gdb_init
 * @return
 */
int dap_stream_ch_gdb_init()
{
    log_it(L_NOTICE, "Global DB exchange channel initialized");
    dap_stream_ch_proc_add(DAP_STREAM_CH_GDB_ID, s_stream_ch_new, s_stream_ch_delete, s_stream_ch_packet_in,
            s_stream_ch_packet_out);
#ifdef DAP_SYS_DEBUG
    for (int i = 0; i < MEMSTAT$K_NR; i++)
        dap_memstat_reg(&s_memstat[i]);
#endif
    return 0;
}

void dap_stream_ch_chain_deinit()
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
#ifdef  DAP_SYS_DEBUG
    atomic_fetch_add(&s_memstat[MEMSTAT$K_STM_CH_CHAIN].alloc_nr, 1);
#endif
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
    if (l_ch_gdb->callback_notify_packet_out)
        l_ch_gdb->callback_notify_packet_out(l_ch_gdb, DAP_STREAM_CH_GDB_PKT_TYPE_DELETE, NULL, l_ch_gdb->callback_notify_arg);
    s_ch_gdb_go_idle(l_ch_gdb);
    debug_if(g_dap_global_db_debug_more, L_NOTICE, "Destroyed GDB sync channel %p with internal data %p", a_ch, l_ch_gdb);
    DAP_DEL_Z(a_ch->internal);
#ifdef  DAP_SYS_DEBUG
    atomic_fetch_add(&s_memstat[MEMSTAT$K_STM_CH_CHAIN].free_nr, 1);
#endif
}

static void s_ch_gdb_go_idle(dap_stream_ch_gdb_t *a_ch_gdb)
{
    if (a_ch_gdb->state == DAP_STREAM_CH_GDB_STATE_IDLE) {
        return;
    }
    a_ch_gdb->state = DAP_STREAM_CH_GDB_STATE_IDLE;

    debug_if(g_dap_global_db_debug_more, L_INFO, "Go in DAP_STREAM_CH_GDB_STATE_IDLE");

    // Cleanup after request
    /*memset(&a_ch_chain->request, 0, sizeof(a_ch_chain->request));
    memset(&a_ch_chain->request_hdr, 0, sizeof(a_ch_chain->request_hdr));
    if (a_ch_chain->request_atom_iter && a_ch_chain->request_atom_iter->chain &&
            a_ch_chain->request_atom_iter->chain->callback_atom_iter_delete) {
        a_ch_chain->request_atom_iter->chain->callback_atom_iter_delete(a_ch_chain->request_atom_iter);
        a_ch_chain->request_atom_iter = NULL;
    }

    dap_stream_ch_chain_hash_item_t *l_hash_item = NULL, *l_tmp = NULL;

    HASH_ITER(hh, a_ch_chain->remote_atoms, l_hash_item, l_tmp) {
        // Clang bug at this, l_hash_item should change at every loop cycle
        HASH_DEL(a_ch_chain->remote_atoms, l_hash_item);
        DAP_DELETE(l_hash_item);
    }
    a_ch_chain->remote_atoms = NULL;
    a_ch_chain->sent_breaks = 0;
    s_free_log_list_gdb(a_ch_chain);*/
}

static void s_process_gossip_msg(dap_proc_thread_t UNUSED_ARG *a_thread, void *a_arg)
{
    assert(a_arg);
    dap_store_obj_t *l_obj = a_arg;
    dap_global_db_cluster_t *l_cluster = dap_global_db_cluster_by_group(dap_global_db_instance_get_default(), l_obj->group);
    if (!l_cluster) {
        log_it(L_WARNING, "An entry in the group %s was rejected because the group name doesn't match any mask.", l_obj->group);
        dap_store_obj_free_one(l_obj);
        return false;
    }
    int l_stream_role = dap_cluster_member_find_role(l_cluster->member_cluster, (dap_stream_node_addr_t *)l_obj->ext);
    if (l_stream_role == -1 && l_cluster->default_role == DAP_GDB_MEMBER_ROLE_NOBODY) {
        debug_if(g_dap_global_db_debug_more, L_WARNING,
                 "Node with addr "NODE_ADDR_FP_STR" isn't a member of closed cluster %s", l_cluster->mnemonim);
        dap_store_obj_free_one(l_obj);
        return false;
    }
    dap_global_db_driver_hash_t l_obj_drv_hash = dap_store_obj_get_driver_hash(l_obj);
    if (dap_global_db_driver_is_hash(a_obj->group, &l_obj_drv_hash)) {
        debug_if(g_dap_global_db_debug_more, L_NOTICE, "Rejected duplicate object with group %s and key %s",
                                            l_obj->group, l_obj->key);
        dap_store_obj_free_one(l_obj);
        return false;
    }
    // Limit time
    uint64_t l_time_store_lim_sec = l_cluster->ttl ? l_cluster->ttl : l_cluster->dbi->store_time_limit * 3600ULL;
    uint64_t l_limit_time = l_time_store_lim_sec ? dap_nanotime_now() - dap_nanotime_from_sec(l_time_store_lim_sec) : 0;
    if (l_limit_time && l_obj->timestamp < l_limit_time) {
        if (g_dap_global_db_debug_more) {
            char l_ts_str[64] = { '\0' };
            dap_time_to_str_rfc822(l_ts_str, sizeof(l_ts_str), dap_nanotime_to_sec(a_obj->timestamp));
            log_it(L_NOTICE, "Rejected too old object with group %s and key %s and timestamp %s",
                                            l_obj->group, l_obj->key, l_ts_str);
        }
        dap_store_obj_free_one(l_obj);
        return false;
    }

    if (g_dap_global_db_debug_more) {
        char l_ts_str[64] = { '\0' };
        dap_time_to_str_rfc822(l_ts_str, sizeof(l_ts_str), dap_nanotime_to_sec(a_obj->timestamp));
        char l_hash_str[DAP_HASH_FAST_STR_SIZE];
        dap_hash_fast_t l_sign_hash;
        if (!l_obj->sign || !dap_sign_get_pkey_hash(l_obj->sign, &l_sign_hash))
            strcpy(l_hash_str, "UNSIGNED");
        else
           dap_hash_fast_to_str(&l_sign_hash, l_hash_str, DAP_HASH_FAST_STR_SIZE);
        log_it(L_DEBUG, "Unpacked log history: group=\"%s\" key=\"%s\""
                " timestamp=\"%s\" value_len=% signer_hash=%s" DAP_UINT64_FORMAT_U,
                a_obj->group, a_obj->key, l_ts_str, a_obj->value_len, l_sign_hash_str);
    }
    dap_global_db_role_t l_signer_role = DAP_GDB_MEMBER_ROLE_NOBODY;
    if (l_obj->sign) {
        dap_stream_node_addr_t l_signer_addr = dap_stream_get_addr_from_sign(l_obj->sign);
        l_signer_role = dap_cluster_member_find_role(l_cluster->member_cluster, l_signer_addr);
    }
    if (l_signer_role == DAP_GDB_MEMBER_ROLE_NOBODY)
        l_signer_role = l_cluster->default_role;
    dap_global_db_role_t l_required_role = DAP_GDB_MEMBER_ROLE_USER;
    if (l_signer_role < l_required_role) {
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Global DB record with group %s and key %s is rejected "
                                                        "with signer role %s and requered role %s",
                                                            l_obj->group, l_obj->key,
                                                            dap_global_db_cluster_role_str(l_signer_role),
                                                            dap_global_db_cluster_role_str(l_reqired_role));
        dap_store_obj_free_one(l_obj);
        return false;
    }
    dap_global_db_optype_t l_obj_type = DAP_GLOBAL_DB_OPTYPE_ADD;
    // Get object with same key and its antinonim from opposite group, if any
    const char l_del_suffix[] = DAP_GLOBAL_DB_DEL_SUFFIX;
    char *l_del_group = NULL, *l_basic_group = NULL;
    size_t l_group_len = strlen(l_obj->group);
    size_t l_unsuffixed_len = l_group_len - sizeof(l_del_suffix) + 1;
    if (l_group_len >= sizeof(l_del_suffix) &&
            !strcmp(l_del_suffix, a_store_obj->group + l_unsuffixed_len)) {
        // It is a group for object destroyers
        l_obj_type = DAP_GLOBAL_DB_OPTYPE_DEL;
        // Only root members can destroy
        l_required_role = DAP_GDB_MEMBER_ROLE_ROOT;
        l_del_group = l_obj->group;
        l_basic_group = strndup(l_obj->group, l_unsuffixed_len);
    } else {
        l_del_group = dap_strdup_printf("%s" DAP_GLOBAL_DB_DEL_SUFFIX, l_obj->group);
        l_basic_group = l_obj->group;
    }
    dap_store_obj_t *l_read_obj = NULL;
    if (dap_global_db_driver_is(l_basic_group, l_obj->key)) {
        l_read_obj = dap_global_db_driver_read(a_obj->group, a_obj->key, NULL);
        if (l_read_obj->flags & DAP_GLOBAL_DB_RECORD_PINNED && l_obj_type == DAP_GLOBAL_DB_OPTYPE_ADD) {
            debug_if(g_dap_global_db_debug_more, L_NOTICE, "Pinned record with group %s and key %s won't be overwritten",
                     l_read_obj->group, l_read_obj->key);
            goto free_n_exit;
        }
        l_required_role = DAP_GDB_MEMBER_ROLE_ROOT; // Need to rewrite existed value
    }
    if (dap_global_db_driver_is(l_del_group, l_obj->key)) {
        if (l_read_obj) {   // Conflict, object is present in both tables
            dap_store_obj_t *l_read_del = dap_global_db_driver_read(l_del, l_obj->key, NULL);
            switch (dap_store_obj_driver_hash_compare(l_read_obj, l_read_del)) {
            case -1:        // Basic obj is older
                if (!(l_read_obj->flags & DAP_GLOBAL_DB_RECORD_PINNED)) {
                    log_it(L_WARNING, "DB record with group %s and key %s will be destroyed to avoid a conflict",
                                                                l_read_obj->group, l_read_obj->key);
                    l_read_obj->type = DAP_GLOBAL_DB_OPTYPE_DEL;
                    dap_global_db_driver_apply(l_read_obj, 1);
                    dap_global_db_cluster_notify(l_cluster, l_read_obj);
                }
                dap_store_obj_free_one(l_read_obj);
                l_read_obj = l_read_del;
                break;
            case 0:         // Objects are the same, omg! Use the basic object
                log_it(L_ERROR, "Duplicate record with group %s and key %s in both local tabels, "
                                                        DAP_GLOBAL_DB_DEL_SUFFIX" will be erased",
                                                            l_read_obj->group, l_read_obj->key);
            case 1:         // Deleted object is older
                debug_if(g_global_db_debug_more, L_WARNING,
                         "DB record with group %s and key %s will be destroyed to avoid a conflict",
                                                                l_read_del->group, l_read_del->key);
                dap_global_db_driver_delete(l_read_del, 1);
                dap_store_obj_free_one(l_read_del);
                break;
            }
        } else
            l_read_obj = dap_global_db_driver_read(l_del_group, l_obj->key, NULL);
    }
    if (l_cluster->owner_root_access && l_obj->sign && l_read_obj->sign &&
            dap_sign_match_pkey_signs(l_obj->sign, l_read_obj->sign))
        l_signer_role = DAP_GDB_MEMBER_ROLE_ROOT;
    if (l_signer_role < l_required_role) {
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Global DB record with group %s and key %s is rejected "
                                                        "with signer role %s and required role %s",
                                                            l_obj->group, l_obj->key,
                                                            dap_global_db_cluster_role_str(l_signer_role),
                                                            dap_global_db_cluster_role_str(l_reqired_role));
        goto free_n_exit;
    }
    switch (dap_store_obj_driver_hash_compare(l_read_obj, l_obj)) {
    case -1:        // Existed obj is older
        debug_if(g_dap_global_db_debug_more, L_INFO, "Applied new global DB record with group %s and key %s",
                                                                    l_obj->group, l_obj->key);
        dap_global_db_driver_apply(l_obj, 1);
        break;
    case 0:         // Objects the same, omg! Use the basic object
        debug_if(g_dap_global_db_debug_more, L_WARNING, "Duplicate record with group %s and key %s not dropped by hash filter",
                                                                    l_obj->group, l_obj->key);
        break;
    case 1:         // Received object is older
        debug_if(g_dap_global_db_debug_more, L_DEBUG, "DB record with group %s and key %s is not applied. It's older than existed record with same key",
                                                        l_read_del->group, l_read_del->key);
        break;
    }
free_n_exit:
    if (l_obj_type == DAP_GLOBAL_DB_OPTYPE_DEL)
        DAP_DELETE(l_basic_group);
    else
        DAP_DELETE(l_del_group);
    if (l_read_obj)
        dap_store_obj_free_one(l_read_obj);
    dap_store_obj_free_one(l_obj);
    return false;
}

static void s_stream_ch_packet_in(dap_stream_ch_t *a_ch, void *a_arg)
{
    dap_stream_ch_gdb_t *l_ch_gdb = DAP_STREAM_CH_GDB(a_ch);
    if (!l_ch_gdb || l_ch_gdb->_inheritor != a_ch) {
        log_it(L_ERROR, "Not valid Global DB channel, returning");
        return;
    }
    dap_stream_ch_pkt_t * l_ch_pkt = (dap_stream_ch_pkt_t *)a_arg;
    s_chain_timer_reset(l_ch_chain);
    switch (l_ch_pkt->hdr.type) {
    case DAP_STREAM_CH_GDB_PKT_TYPE_GOSSIP: {
        dap_global_db_pkt_t *l_pkt = (dap_global_db_pkt_t *)l_ch_pkt->data;
        dap_store_obj_t *l_obj = dap_global_db_pkt_deserialize(l_pkt, l_ch_pkt->hdr.data_size, a_ch->stream->node);
        if (!l_obj || !dap_global_db_pkt_check_sign_crc(l_pkt)) {
            log_it(L_WARNING, "Wrong Global DB gossip packet rejected");
            break;
        }
        dap_proc_thread_callback_add_pri(NULL, s_process_gossip_msg, l_obj, DAP_GLOBAL_DB_TASK_PRIORITY);
    } break;







        // Request for gdbs list update
        case DAP_STREAM_CH_CHAIN_PKT_TYPE_UPDATE_GLOBAL_DB_REQ:{
            if(l_ch_chain->state != CHAIN_STATE_IDLE){
                log_it(L_WARNING, "Can't process UPDATE_GLOBAL_DB_REQ request because its already busy with syncronization");
                dap_stream_ch_chain_pkt_write_error_unsafe(a_ch, l_chain_pkt->hdr.net_id.uint64,
                        l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64,
                        "ERROR_SYNC_REQUEST_ALREADY_IN_PROCESS");
                break;
            }
            log_it(L_INFO, "In:  UPDATE_GLOBAL_DB_REQ pkt: net 0x%016"DAP_UINT64_FORMAT_x" chain 0x%016"DAP_UINT64_FORMAT_x" cell 0x%016"DAP_UINT64_FORMAT_x,
                            l_chain_pkt->hdr.net_id.uint64, l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64);
            if (l_chain_pkt_data_size == (size_t)sizeof(dap_stream_ch_chain_sync_request_t))
                l_ch_chain->request = *(dap_stream_ch_chain_sync_request_t*)l_chain_pkt->data;
            dap_chain_node_client_t *l_client = (dap_chain_node_client_t *)l_ch_chain->callback_notify_arg;
            if (l_client && l_client->resync_gdb)
                l_ch_chain->request.id_start = 0;
            else
                l_ch_chain->request.id_start = 1;   // incremental sync by default
            struct sync_request *l_sync_request = dap_stream_ch_chain_create_sync_request(l_chain_pkt, a_ch);
            l_ch_chain->stats_request_gdb_processed = 0;
            l_ch_chain->request_hdr = l_chain_pkt->hdr;
            dap_proc_thread_callback_add(a_ch->stream_worker->worker->proc_queue_input, s_sync_update_gdb_proc_callback, l_sync_request);
        } break;

        // Response with metadata organized in TSD
        case DAP_STREAM_CH_CHAIN_PKT_TYPE_UPDATE_GLOBAL_DB_TSD: {
            if (l_chain_pkt_data_size) {
                dap_tsd_t *l_tsd_rec = (dap_tsd_t *)l_chain_pkt->data;
                if (l_tsd_rec->type != DAP_STREAM_CH_CHAIN_PKT_TYPE_UPDATE_TSD_LAST_ID ||
                        l_tsd_rec->size < 2 * sizeof(uint64_t) + 2) {
                    break;
                }
                void *l_data_ptr = l_tsd_rec->data;
                uint64_t l_node_addr = *(uint64_t *)l_data_ptr;
                l_data_ptr += sizeof(uint64_t);
                uint64_t l_last_id = *(uint64_t *)l_data_ptr;
                l_data_ptr += sizeof(uint64_t);
                char *l_group = (char *)l_data_ptr;
                dap_db_set_last_id_remote(l_node_addr, l_last_id, l_group);
                if (s_debug_more) {
                    dap_chain_node_addr_t l_addr;
                    l_addr.uint64 = l_node_addr;
                    log_it(L_INFO, "Set last_id %"DAP_UINT64_FORMAT_U" for group %s for node "NODE_ADDR_FP_STR,
                                    l_last_id, l_group, NODE_ADDR_FP_ARGS_S(l_addr));
                }
            } else if (s_debug_more)
                log_it(L_DEBUG, "Global DB TSD packet detected");
        } break;

        // If requested - begin to recieve record's hashes
        case DAP_STREAM_CH_CHAIN_PKT_TYPE_UPDATE_GLOBAL_DB_START:{
            if (s_debug_more)
                log_it(L_INFO, "In:  UPDATE_GLOBAL_DB_START pkt net 0x%016"DAP_UINT64_FORMAT_x" chain 0x%016"DAP_UINT64_FORMAT_x" cell 0x%016"DAP_UINT64_FORMAT_x,
                                l_chain_pkt->hdr.net_id.uint64, l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64);
            if (l_ch_chain->state != CHAIN_STATE_IDLE){
                log_it(L_WARNING, "Can't process UPDATE_GLOBAL_DB_START request because its already busy with syncronization");
                dap_stream_ch_chain_pkt_write_error_unsafe(a_ch, l_chain_pkt->hdr.net_id.uint64,
                        l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64,
                        "ERROR_SYNC_REQUEST_ALREADY_IN_PROCESS");
                break;
            }
            l_ch_chain->request_hdr = l_chain_pkt->hdr;
            l_ch_chain->state = CHAIN_STATE_UPDATE_GLOBAL_DB_REMOTE;
        } break;
        // Response with gdb element hashes and sizes
        case DAP_STREAM_CH_CHAIN_PKT_TYPE_UPDATE_GLOBAL_DB:{
            if(s_debug_more)
                log_it(L_INFO, "In: UPDATE_GLOBAL_DB pkt data_size=%zu", l_chain_pkt_data_size);
            if (l_ch_chain->state != CHAIN_STATE_UPDATE_GLOBAL_DB_REMOTE ||
                    memcmp(&l_ch_chain->request_hdr, &l_chain_pkt->hdr, sizeof(dap_stream_ch_chain_pkt_t))) {
                log_it(L_WARNING, "Can't process UPDATE_GLOBAL_DB request because its already busy with syncronization");
                s_stream_ch_write_error_unsafe(a_ch, l_chain_pkt->hdr.net_id.uint64,
                        l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64,
                        "ERROR_SYNC_REQUEST_ALREADY_IN_PROCESS");
                break;
            }
            for ( dap_stream_ch_chain_update_element_t * l_element =(dap_stream_ch_chain_update_element_t *) l_chain_pkt->data;
                   (size_t) (((byte_t*)l_element) - l_chain_pkt->data ) < l_chain_pkt_data_size;
                  l_element++){
                dap_stream_ch_chain_hash_item_t * l_hash_item = NULL;
                unsigned l_hash_item_hashv;
                HASH_VALUE(&l_element->hash, sizeof(l_element->hash), l_hash_item_hashv);
                HASH_FIND_BYHASHVALUE(hh, l_ch_chain->remote_gdbs, &l_element->hash, sizeof(l_element->hash),
                                      l_hash_item_hashv, l_hash_item);
                if (!l_hash_item) {
                    l_hash_item = DAP_NEW_Z(dap_stream_ch_chain_hash_item_t);
                    if (!l_hash_item) {
                        log_it(L_CRITICAL, "Memory allocation error");
                        return;
                    }
                    l_hash_item->hash = l_element->hash;
                    l_hash_item->size = l_element->size;
                    HASH_ADD_BYHASHVALUE(hh, l_ch_chain->remote_gdbs, hash, sizeof(l_hash_item->hash),
                                         l_hash_item_hashv, l_hash_item);
                    /*if (s_debug_more){
                        char l_hash_str[72]={ [0]='\0'};
                        dap_chain_hash_fast_to_str(&l_hash_item->hash,l_hash_str,sizeof (l_hash_str));
                        log_it(L_DEBUG,"In: Updated remote hash gdb list with %s ", l_hash_str);
                    }*/
                }
            }
        } break;
        // End of response with starting of DB sync
        case DAP_STREAM_CH_CHAIN_PKT_TYPE_SYNC_GLOBAL_DB:
        case DAP_STREAM_CH_CHAIN_PKT_TYPE_UPDATE_GLOBAL_DB_END: {
            if(l_chain_pkt_data_size == sizeof(dap_stream_ch_chain_sync_request_t)) {
                if (l_ch_pkt->hdr.type == DAP_STREAM_CH_CHAIN_PKT_TYPE_SYNC_GLOBAL_DB && l_ch_chain->state != CHAIN_STATE_IDLE) {
                    log_it(L_WARNING, "Can't process SYNC_GLOBAL_DB request because not in idle state");
                    dap_stream_ch_chain_pkt_write_error_unsafe(a_ch, l_chain_pkt->hdr.net_id.uint64,
                            l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64,
                            "ERROR_STATE_NOT_IN_IDLE");
                    break;
                }
                if (l_ch_pkt->hdr.type == DAP_STREAM_CH_CHAIN_PKT_TYPE_UPDATE_GLOBAL_DB_END &&
                        (l_ch_chain->state != CHAIN_STATE_UPDATE_GLOBAL_DB_REMOTE ||
                        memcmp(&l_ch_chain->request_hdr, &l_chain_pkt->hdr, sizeof(dap_stream_ch_chain_pkt_t)))) {
                    log_it(L_WARNING, "Can't process UPDATE_GLOBAL_DB_END request because its already busy with syncronization");
                    s_stream_ch_write_error_unsafe(a_ch, l_chain_pkt->hdr.net_id.uint64,
                            l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64,
                            "ERROR_SYNC_REQUEST_ALREADY_IN_PROCESS");
                    break;
                }
                if(s_debug_more)
                {
                    if (l_ch_pkt->hdr.type == DAP_STREAM_CH_CHAIN_PKT_TYPE_UPDATE_GLOBAL_DB_END)
                        log_it(L_INFO, "In: UPDATE_GLOBAL_DB_END pkt with total count %d hashes",
                                        HASH_COUNT(l_ch_chain->remote_gdbs));
                    else
                        log_it(L_INFO, "In: SYNC_GLOBAL_DB pkt");
                }
                if (l_chain_pkt_data_size == sizeof(dap_stream_ch_chain_sync_request_t))
                    l_ch_chain->request = *(dap_stream_ch_chain_sync_request_t*)l_chain_pkt->data;
                struct sync_request *l_sync_request = dap_stream_ch_chain_create_sync_request(l_chain_pkt, a_ch);
                dap_proc_thread_callback_add(a_ch->stream_worker->worker->proc_queue_input, s_sync_out_gdb_proc_callback, l_sync_request);
            }else{
                log_it(L_WARNING, "DAP_STREAM_CH_CHAIN_PKT_TYPE_SYNC_GLOBAL_DB: Wrong chain packet size %zd when expected %zd", l_chain_pkt_data_size, sizeof(l_ch_chain->request));
                s_stream_ch_write_error_unsafe(a_ch, l_chain_pkt->hdr.net_id.uint64,
                        l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64,
                        "ERROR_CHAIN_PKT_DATA_SIZE" );
            }
        } break;
        // first packet of data with source node address
        case DAP_STREAM_CH_CHAIN_PKT_TYPE_FIRST_GLOBAL_DB: {
            if(l_chain_pkt_data_size == (size_t)sizeof(dap_chain_node_addr_t)){
               l_ch_chain->request.node_addr = *(dap_chain_node_addr_t*)l_chain_pkt->data;
               l_ch_chain->stats_request_gdb_processed = 0;
               log_it(L_INFO, "In: FIRST_GLOBAL_DB data_size=%zu net 0x%016"DAP_UINT64_FORMAT_x" chain 0x%016"DAP_UINT64_FORMAT_x" cell 0x%016"DAP_UINT64_FORMAT_x
                              " from address "NODE_ADDR_FP_STR, l_chain_pkt_data_size,   l_chain_pkt->hdr.net_id.uint64 ,
                              l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64, NODE_ADDR_FP_ARGS_S(l_ch_chain->request.node_addr) );
            }else {
               log_it(L_WARNING,"Incorrect data size %zu in packet DAP_STREAM_CH_CHAIN_PKT_TYPE_FIRST_GLOBAL_DB", l_chain_pkt_data_size);
               s_stream_ch_write_error_unsafe(a_ch, l_chain_pkt->hdr.net_id.uint64,
                       l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64,
                       "ERROR_CHAIN_PACKET_TYPE_FIRST_GLOBAL_DB_INCORRET_DATA_SIZE");
            }
        } break;

        case DAP_STREAM_CH_CHAIN_PKT_TYPE_GLOBAL_DB: {
            if(s_debug_more)
                log_it(L_INFO, "In: GLOBAL_DB data_size=%zu", l_chain_pkt_data_size);
            // get transaction and save it to global_db
            if(l_chain_pkt_data_size > 0) {
                struct sync_request *l_sync_request = dap_stream_ch_chain_create_sync_request(l_chain_pkt, a_ch);
                dap_chain_pkt_item_t *l_pkt_item = &l_sync_request->pkt;
                l_pkt_item->pkt_data = DAP_DUP_SIZE(l_chain_pkt->data, l_chain_pkt_data_size);
                l_pkt_item->pkt_data_size = l_chain_pkt_data_size;
                dap_proc_thread_callback_add(a_ch->stream_worker->worker->proc_queue_input, s_gdb_in_pkt_proc_callback, l_sync_request);
            } else {
                log_it(L_WARNING, "Packet with GLOBAL_DB atom has zero body size");
                s_stream_ch_write_error_unsafe(a_ch, l_chain_pkt->hdr.net_id.uint64,
                        l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64,
                        "ERROR_GLOBAL_DB_PACKET_EMPTY");
            }
        }  break;

        case DAP_STREAM_CH_CHAIN_PKT_TYPE_SYNCED_GLOBAL_DB: {
                log_it(L_INFO, "In:  SYNCED_GLOBAL_DB: net 0x%016"DAP_UINT64_FORMAT_x" chain 0x%016"DAP_UINT64_FORMAT_x" cell 0x%016"DAP_UINT64_FORMAT_x,
                                l_chain_pkt->hdr.net_id.uint64, l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64);
                if (!l_ch_chain->callback_notify_packet_in) { // we haven't node client waitng, so reply to other side
                    dap_stream_ch_chain_sync_request_t l_sync_gdb = {};
                    dap_chain_net_t *l_net = dap_chain_net_by_id(l_chain_pkt->hdr.net_id);
                    l_sync_gdb.node_addr.uint64 = dap_chain_net_get_cur_addr_int(l_net);
                    dap_stream_ch_chain_pkt_write_unsafe(a_ch, DAP_STREAM_CH_CHAIN_PKT_TYPE_UPDATE_GLOBAL_DB_REQ, l_chain_pkt->hdr.net_id.uint64,
                                                  l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64, &l_sync_gdb, sizeof(l_sync_gdb));
                }
        } break;

        case DAP_STREAM_CH_CHAIN_PKT_TYPE_SYNC_GLOBAL_DB_RVRS: {
            dap_stream_ch_chain_sync_request_t l_sync_gdb = {};
            l_sync_gdb.id_start = 1;
            dap_chain_net_t *l_net = dap_chain_net_by_id(l_chain_pkt->hdr.net_id);
            l_sync_gdb.node_addr.uint64 = dap_chain_net_get_cur_addr_int(l_net);
            log_it(L_INFO, "In:  SYNC_GLOBAL_DB_RVRS pkt: net 0x%016"DAP_UINT64_FORMAT_x" chain 0x%016"DAP_UINT64_FORMAT_x" cell 0x%016"DAP_UINT64_FORMAT_x
                           ", request gdb sync from %"DAP_UINT64_FORMAT_U, l_chain_pkt->hdr.net_id.uint64 , l_chain_pkt->hdr.chain_id.uint64,
                           l_chain_pkt->hdr.cell_id.uint64, l_sync_gdb.id_start );
            dap_stream_ch_chain_pkt_write_unsafe(a_ch, DAP_STREAM_CH_CHAIN_PKT_TYPE_SYNC_GLOBAL_DB, l_chain_pkt->hdr.net_id.uint64,
                                          l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64, &l_sync_gdb, sizeof(l_sync_gdb));
        } break;

        case DAP_STREAM_CH_CHAIN_PKT_TYPE_SYNCED_GLOBAL_DB_GROUP: {
            if (s_debug_more)
                log_it(L_INFO, "In:  SYNCED_GLOBAL_DB_GROUP pkt net 0x%016"DAP_UINT64_FORMAT_x" chain 0x%016"DAP_UINT64_FORMAT_x" cell 0x%016"DAP_UINT64_FORMAT_x,
                                l_chain_pkt->hdr.net_id.uint64, l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64);
        } break;
        case DAP_STREAM_CH_CHAIN_PKT_TYPE_FIRST_GLOBAL_DB_GROUP: {
            if (s_debug_more)
                log_it(L_INFO, "In:  FIRST_GLOBAL_DB_GROUP pkt net 0x%016"DAP_UINT64_FORMAT_x" chain 0x%016"DAP_UINT64_FORMAT_x" cell 0x%016"DAP_UINT64_FORMAT_x,
                                l_chain_pkt->hdr.net_id.uint64, l_chain_pkt->hdr.chain_id.uint64, l_chain_pkt->hdr.cell_id.uint64);
        } break;

}

static void s_stream_ch_packet_out(dap_stream_ch_t *a_ch, void *a_arg)
{

}

static void s_stream_ch_io_complete(dap_events_socket_t *a_es, void *a_arg, int a_errno)
{

}
