/*
* Authors:
* Roman Khlopkov <roman.khlopkov@demlabs.net>
* Pavel Uhanov <pavel.uhanov@demlabs.net>
* Cellframe       https://cellframe.net
* DeM Labs Inc.   https://demlabs.net
* Copyright  (c) 2017-2024
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

#include "dap_link_manager.h"
#include "dap_global_db.h"
#include "dap_global_db_cluster.h"
#include "dap_stream_cluster.h"
#include "dap_worker.h"
#include "dap_config.h"

#define LOG_TAG "dap_link_manager"

static uint32_t s_timer_update_states = 4000;
static uint32_t s_min_links_num = 5;
static dap_link_manager_t *s_link_manager = NULL;

static void s_client_connect(dap_link_t *a_link, const char *a_active_channels, dap_client_callback_t a_connected_callback);

/**
 * @brief s_client_connected_links_cluster_callback
 * @param a_client
 * @param a_arg
 */
static void s_client_connected_links_cluster_callback(dap_client_t *a_client, void *a_arg)
{
// sanity check
    dap_return_if_pass(!DAP_LINK(a_client));
// func work
    dap_link_t *l_link = DAP_LINK(a_client);
    dap_list_t *l_item = NULL;
    DL_FOREACH(l_link->links_clusters, l_item) {
        dap_cluster_member_add((dap_cluster_t *)l_item->data, &l_link->node_addr, 0, NULL);
    }
    log_it(L_NOTICE, "Stream connection with node "NODE_ADDR_FP_STR" (%s:%hu) established",
                NODE_ADDR_FP_ARGS_S(l_link->node_addr),
                l_link->host_addr_str, l_link->host_port);
    // if(l_link->link_manager->callbacks.connected)
    //     l_link->link_manager->callbacks.connected(l_link, NULL /*l_node_client->callbacks_arg*/);
    // dap_stream_ch_chain_net_pkt_hdr_t l_announce = { .version = DAP_STREAM_CH_CHAIN_NET_PKT_VERSION,
    //                                                  .net_id  = l_node_client->net->pub.id };
    // dap_client_write_unsafe(a_client, 'N', DAP_STREAM_CH_CHAIN_NET_PKT_TYPE_ANNOUNCE,
    //                                  &l_announce, sizeof(l_announce));
    l_link->state = LINK_STATE_ESTABLISHED;
}

/**
 * @brief s_client_error_callback
 * @param a_client
 * @param a_arg
 */
static void s_client_error_callback(dap_client_t *a_client, void *a_arg)
{
// sanity check
    dap_return_if_pass(!DAP_LINK(a_client));
// func work
    dap_link_t *l_link = DAP_LINK(a_client);
    // check for last attempt
    bool l_is_last_attempt = a_arg ? true : false;
    if (l_is_last_attempt) {
        l_link->state = LINK_STATE_DISCONNECTED;

        if (l_link->link_manager->callbacks.disconnected) {
            l_link->link_manager->callbacks.disconnected(NULL, NULL);
        }
        if (l_link->keep_connection) {
            if (dap_client_get_stage(l_link->client) != STAGE_BEGIN)
                dap_client_go_stage(l_link->client, STAGE_BEGIN, NULL);
            log_it(L_INFO, "Reconnecting node client with peer "NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS_S(l_link->node_addr));
            l_link->state = LINK_STATE_CONNECTING ;
            dap_client_go_stage(l_link->client, STAGE_STREAM_STREAMING, s_client_connected_links_cluster_callback);
        }
    } else if(l_link->link_manager->callbacks.error) // TODO make different error codes
        l_link->link_manager->callbacks.error(l_link, EINVAL, NULL /*l_node_client->callbacks_arg*/);
}

static void s_client_delete_callback(UNUSED_ARG dap_client_t *a_client, void *a_arg)
{
    // TODO make decision for possible client replacement
    assert(a_arg);
    dap_chain_node_client_close_unsafe(a_arg);
}

static void s_delete_callback(dap_link_manager_t *a_manager)
{
    
}

bool s_update_states(void *a_arg)
{
// sanity check
    dap_link_manager_t *l_link_manager = (dap_link_manager_t *)a_arg;
    dap_return_val_if_pass(!l_link_manager, true);
// func work
    if(!l_link_manager->active || !l_link_manager->active_nets) {
        return true;
    }
    dap_link_t *l_link = NULL, *l_tmp = NULL;
    // pthread_rwlock_wrlock(&a_cluster->members_lock);
    HASH_ITER(hh, l_link_manager->self_links, l_link, l_tmp) {
        // if we don't have any connections with members in role clusters then create connection
        if(!l_link->client && l_link->role_clusters) {
            if (!l_link_manager->callbacks.fill_net_info(l_link)) {
                s_client_connect(l_link, "GND", s_client_connected_links_cluster_callback);
            } else {
                log_it(L_INFO, "Can't find node "NODE_ADDR_FP_STR" in node list", NODE_ADDR_FP_ARGS_S(l_link->node_addr));
            }
        } else if (l_link->client && !l_link->role_clusters) {
            // recheck dynamic cluster and if no need close connect
        }
    }
    return true;
}

int dap_link_manager_init(const dap_link_manager_callbacks_t *a_callbacks)
{
// sanity check
    dap_return_val_if_pass(s_link_manager, -2);
// func work
    s_timer_update_states = dap_config_get_item_uint32_default(g_config, "link_manager", "timer_update_states", s_timer_update_states);
    s_min_links_num = dap_config_get_item_uint32_default(g_config, "link_manager", "min_links_num", s_min_links_num);
    if (!(s_link_manager = dap_link_manager_new(a_callbacks))) {
        log_it(L_ERROR, "Default link manager not inited");
        return -1;
    }
    return 0;
}

void dap_link_manager_deinit()
{
    dap_list_free(s_link_manager->active_nets);
}

dap_link_manager_t *dap_link_manager_new(const dap_link_manager_callbacks_t *a_callbacks)
{
// sanity check
    dap_return_val_if_pass_err(!a_callbacks || !a_callbacks->fill_net_info, NULL, "Needed link manager callbacks not filled, please check it");
// memory alloc
    dap_link_manager_t *l_ret = NULL;
    DAP_NEW_Z_RET_VAL(l_ret, dap_link_manager_t, NULL, NULL);
// func work
    l_ret->callbacks = *a_callbacks;
    l_ret->update_timer = dap_timerfd_start(s_timer_update_states, s_update_states, l_ret);
    if(!l_ret->update_timer)
        log_it(L_WARNING, "Link manager created, but timer not active");
    if(!l_ret->callbacks.delete)
        l_ret->callbacks.delete = s_delete_callback;
    l_ret->min_links_num = s_min_links_num;
    l_ret->active = true;
    return l_ret;
}

DAP_INLINE dap_link_manager_t *dap_link_manager_get_default()
{
    return s_link_manager;
}

DAP_INLINE void dap_link_manager_add_active_net(char *a_net_name)
{
    dap_return_if_pass(!s_link_manager || !a_net_name);
    s_link_manager->active_nets = dap_list_append(s_link_manager->active_nets, (void *)a_net_name);
}

DAP_INLINE void dap_link_manager_remove_active_net(char *a_net_name)
{
    dap_return_if_pass(!s_link_manager || !a_net_name);
    s_link_manager->active_nets = dap_list_remove(s_link_manager->active_nets, (void *)a_net_name);
}

void dap_link_manager_add_role_cluster(dap_cluster_member_t *a_member)
{
    dap_return_if_pass(!s_link_manager || !a_member || !a_member->cluster);
    // pthread_rwlock_wrlock(&it->members_lock);
    dap_link_t *l_link = NULL;
    HASH_FIND(hh, s_link_manager->self_links, &a_member->addr, sizeof(a_member->addr), l_link);
    if (!l_link) {
        DAP_NEW_Z_RET(l_link, dap_link_t, NULL);
        l_link->node_addr.uint64 = a_member->addr.uint64;
        HASH_ADD(hh, s_link_manager->self_links, node_addr, sizeof(l_link->node_addr), l_link);
    }
    l_link->role_clusters = dap_list_append(l_link->role_clusters, a_member->cluster);
    // pthread_rwlock_unlock(&it->members_lock);
}

void dap_link_manager_add_links_cluster(dap_cluster_member_t *a_member)
{
    dap_return_if_pass(!s_link_manager || !a_member || !a_member->cluster);
    // pthread_rwlock_wrlock(&it->members_lock);
    dap_link_t *l_link = NULL;
    HASH_FIND(hh, s_link_manager->self_links, &a_member->addr, sizeof(a_member->addr), l_link);
    if (!l_link) {
        DAP_NEW_Z_RET(l_link, dap_link_t, NULL);
        l_link->node_addr.uint64 = a_member->addr.uint64;
        HASH_ADD(hh, s_link_manager->self_links, node_addr, sizeof(l_link->node_addr), l_link);
    }
    l_link->links_clusters = dap_list_append(l_link->links_clusters, a_member->cluster);
    // pthread_rwlock_unlock(&it->members_lock);
}

void dap_link_manager_remove_role_cluster(dap_cluster_member_t *a_member)
{
    dap_return_if_pass(!s_link_manager || !a_member || !a_member->cluster);
    // pthread_rwlock_wrlock(&it->members_lock);
    dap_link_t *l_link = NULL;
    HASH_FIND(hh, s_link_manager->self_links, &a_member->addr, sizeof(a_member->addr), l_link);
    if (!l_link) {
        log_it(L_ERROR, "Try cluster deleting from non-existent link");
        return;
    }
    l_link->role_clusters = dap_list_remove(l_link->role_clusters, a_member->cluster);
    // pthread_rwlock_unlock(&it->members_lock);
}

void dap_link_manager_remove_links_cluster(dap_cluster_member_t *a_member)
{
    dap_return_if_pass(!s_link_manager || !a_member || !a_member->cluster);
    // pthread_rwlock_wrlock(&it->members_lock);
    dap_link_t *l_link = NULL;
    HASH_FIND(hh, s_link_manager->self_links, &a_member->addr, sizeof(a_member->addr), l_link);
    if (!l_link) {
        log_it(L_ERROR, "Try cluster deleting from non-existent link");
        return;
    }
    l_link->links_clusters = dap_list_remove(l_link->links_clusters, a_member->cluster);
    // pthread_rwlock_unlock(&it->members_lock);
}

/**
 * @brief dap_chain_node_client_connect
 * Create new dap_client, setup it, and send it in adventure trip
 * @param a_node_client dap_chain_node_client_t
 * @param a_active_channels a_active_channels
 * @param a_link_cluster - cluster to added node addr if connected
 */
void s_client_connect(dap_link_t *a_link, const char *a_active_channels, dap_client_callback_t a_connected_callback)
{
// sanity check 
    dap_return_if_pass(!a_link); 
//func work
    a_link->client = dap_client_new(s_client_delete_callback, s_client_error_callback, NULL);
    dap_client_set_is_always_reconnect(a_link->client, false);
    a_link->client->_inheritor = a_link;
    dap_client_set_active_channels_unsafe(a_link->client, a_active_channels);
    log_it(L_INFO, "Connecting to addr %s : %d", a_link->host_addr_str, a_link->host_port);
    dap_client_set_uplink_unsafe(a_link->client, a_link->host_addr_str, a_link->host_port);
    a_link->state = LINK_STATE_CONNECTING;
    // Handshake & connect
    dap_client_go_stage(a_link->client, STAGE_STREAM_STREAMING, a_connected_callback);
    return;
}