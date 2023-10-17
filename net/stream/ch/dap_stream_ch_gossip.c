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
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_ch_gossip.h"
#include "dap_stream_cluster.h"

#define LOG_TAG "dap_stream_ch_gossip"

struct gossip_callbacks {
    uint8_t ch_id;
    dap_gossip_callback_check_t callback_check;
    dap_gossip_callback_payload_t callback_payload;
};
static dap_list_t *s_gossip_callbacks_list = NULL;

static void s_stream_ch_packet_in(dap_stream_ch_t *a_ch, void *a_arg);
/**
 * @brief dap_stream_ch_gdb_init
 * @return
 */
int dap_stream_ch_gossip_init()
{
    log_it(L_NOTICE, "Global DB exchange channel initialized");
    dap_stream_ch_proc_add(DAP_STREAM_CH_GOSSIP_ID, NULL, NULL, s_stream_ch_packet_in, NULL);
    return 0;
}

void dap_stream_ch_gossip_deinit()
{

}

void dap_gossip_msg_retranslate(dap_cluster_t *a_cluster, dap_gossip_msg_t *a_msg)
{
    dap_return_if_fail(a_msg && !a_msg->payload_len);
    assert(!memcmp(&a_cluster->uuid, &a_msg->cluster_id, sizeof(dap_guuid_t));
    dap_stream_node_addr_t *l_tracepath = a_msg->hash_n_trace_n_payload + a_msg->payload_hash_len;
    size_t l_step_count = a_msg->trace_len / sizeof(dap_stream_node_addr_t);
    size_t l_msg_new_size = dap_gossip_msg_size_get(a_msg) + sizeof(g_node_addr);
    dap_gossip_msg_t *l_msg_new = DAP_NEW_Z_SIZE(dap_gossip_msg_t, l_msg_new_size);
    *l_msg_new = *a_msg;
    l_msg_new->trace_len += sizeof(g_node_addr);
    dap_stream_node_addr_t *l_new_addr_ptr = (dap_stream_node_addr_t *)(l_msg_new->hash_n_trace_n_payload +
                                                                        a_msg->payload_hash_len + a_msg->trace_len);
    *l_new_addr_ptr = g_node_addr;
    dap_cluster_broadcast(a_cluster, DAP_STREAM_CH_GOSSIP_ID, DAP_STREAM_CH_GOSSIP_MSG_TYPE_HASH,
                          l_msg_new, l_msg_new_size, l_tracepath, l_step_count);
}

static struct gossip_callbacks *s_get_callbacks_by_ch_id(a_ch_id)
{
    for (dap_list_t *it = s_gossip_callbacks_list; it; it = it->next) {
        struct gossip_callbacks *l_callbacks = it->data;
        if (l_callbacks->ch_id == a_ch_id)
            return l_callbacks;
    }
    return NULL;
}

static void s_stream_ch_packet_in(dap_stream_ch_t *a_ch, void *a_arg)
{
    dap_stream_ch_pkt_t *l_ch_pkt = (dap_stream_ch_pkt_t *)a_arg;
    switch (l_ch_pkt->hdr.type) {

    case DAP_STREAM_CH_GOSSIP_MSG_TYPE_REQUEST:
        break;
    case DAP_STREAM_CH_GOSSIP_MSG_TYPE_HASH:
    case DAP_STREAM_CH_GOSSIP_MSG_TYPE_DATA: {
        dap_gossip_msg_t *l_msg = (dap_gossip_msg_t *)l_ch_pkt->data;
        if (l_ch_pkt->hdr.data_size < sizeof(dap_stream_gossip_msg_t)) {
            log_it(L_WARNING, "Incorrect gossip message data size %zu, must be at least %zu",
                                                l_ch_pkt->hdr.data_size, sizeof(dap_stream_gossip_msg_t));
            break;
        }
        if (l_ch_pkt->hdr.data_size != dap_gossip_msg_size_get(l_msg)) {
            log_it(L_WARNING, "Incorrect gossip message data size %zu, expected %zu",
                                                l_ch_pkt->hdr.data_size, dap_gossip_msg_size_get(l_msg));
            break;
        }
        if (l_msg->version != DAP_GOSSIP_CURRENT_VERSION) {
            log_it(L_ERROR, "Incorrect gossip protocol version %hhu, current version is %u",
                                                         l_msg->version, DAP_GOSSIP_CURRENT_VERSION);
            break;
        }
        if (l_msg->trace_len % sizeof(dap_stream_node_addr_t) != 0) {
            log_it(L_WARNING, "Unaligned gossip message tracepath size %zu",
                                                l_msg->trace_len);
            break;
        }
        dap_cluster_t *l_links_cluster = dap_cluster_find(l_msg->cluster_id);
        if (!l_links_cluster) {
            log_it(L_ERROR, "Can't find cluster for gossip message propagating");
            break;
        }
        dap_cluster_member_t *l_check = dap_cluster_member_find_unsafe(l_links_cluster, &a_ch->stream->node);
        if (!l_check) {
            debug_if(g_dap_global_db_debug_more, L_WARNING,
                     "Node with addr "NODE_ADDR_FP_STR" isn't a member of cluster %s",
                     NODE_ADDR_FP_ARGS_S(a_ch->stream->node), l_links_cluster->mnemonim);
            dap_store_obj_free_one(l_obj);
            return false;
        }
        struct gossip_callbacks *l_callbacks = s_get_callbacks_by_ch_id(l_msg->payload_ch_id);
        if (!l_callbacks) {
            log_it(L_ERROR, "Can't find channel callbacks for gossip message propagating");
            break;
        }
        assert(l_callbacks->callback_check);
        assert(l_callbacks->callback_payload);
        if (!l_callbacks->callback_check(l_msg->hash_n_trace_n_payload, l_msg->payload_hash_len)) {
            // Looks like a double. Just ignore it
            break;
        }
        if (l_msg->payload_len) {
            void *l_payload = l_msg->hash_n_trace_n_payload + l_msg->payload_hash_len + l_msg->trace_len;
            l_callbacks->callback_payload(l_payload, l_msg->payload_len, l_msg->hash_n_trace_n_payload);
        } else if (l_ch_pkt->hdr.type == DAP_STREAM_CH_GOSSIP_MSG_TYPE_HASH)
            dap_gossip_msg_retranslate(l_cluster, l_msg);
        else
            log_it(L_WARNING, "NULL payload in gossip data message");
    } break;

    default:
        log_it(L_WARNING, "Unknown gossip packet type %hhu", l_ch_pkt->hdr.type);
        break;
    }
}

/*



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
                    if (s_debug_more){
                        char l_hash_str[72]={ [0]='\0'};
                        dap_chain_hash_fast_to_str(&l_hash_item->hash,l_hash_str,sizeof (l_hash_str));
                        log_it(L_DEBUG,"In: Updated remote hash gdb list with %s ", l_hash_str);
                    }
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
    */

static void s_stream_ch_packet_out(dap_stream_ch_t *a_ch, void *a_arg)
{

}

static void s_stream_ch_io_complete(dap_events_socket_t *a_es, void *a_arg, int a_errno)
{

}
