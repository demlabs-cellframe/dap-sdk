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
#include "utlist.h"

#define LOG_TAG "dap_link_manager"

static uint32_t s_timer_update_states = 4000;
static uint32_t s_min_links_num = 5;
static dap_link_manager_t *s_link_manager = NULL;

static bool s_client_connect(dap_link_t *a_link, const char *a_active_channels);

/**
 * @brief a_stage_end_callback
 * @param a_client
 * @param a_arg
 */
static void s_client_connected_callback(dap_client_t *a_client, UNUSED_ARG void *a_arg)
{
    dap_link_t *l_link = DAP_LINK(a_client);
    if(l_link) {
        char l_ip_addr_str[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &l_link->addr_v4, l_ip_addr_str, INET_ADDRSTRLEN);
        log_it(L_NOTICE, "Stream connection with node "NODE_ADDR_FP_STR" (%s:%hu) established",
                    NODE_ADDR_FP_ARGS_S(l_link->addr),
                    l_ip_addr_str, l_link->port);

        // if(l_link->link_manager->callbacks.connected)
        //     l_link->link_manager->callbacks.connected(l_link, NULL /*l_node_client->callbacks_arg*/);
        // dap_stream_ch_chain_net_pkt_hdr_t l_announce = { .version = DAP_STREAM_CH_CHAIN_NET_PKT_VERSION,
        //                                                  .net_id  = l_node_client->net->pub.id };
        // dap_client_write_unsafe(a_client, 'N', DAP_STREAM_CH_CHAIN_NET_PKT_TYPE_ANNOUNCE,
        //                                  &l_announce, sizeof(l_announce));
        l_link->state = LINK_STATE_ESTABLISHED;
    }
}

/**
 * @brief s_client_error_callback
 * @param a_client
 * @param a_arg
 */
static void s_client_error_callback(dap_client_t *a_client, void *a_arg)
{
// sanity check
    dap_return_if_pass(!a_client || !a_client->_inheritor);
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
            log_it(L_INFO, "Reconnecting node client with peer "NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS_S(l_link->addr));
            l_link->state = LINK_STATE_CONNECTING ;
            dap_client_go_stage(l_link->client, STAGE_STREAM_STREAMING, s_client_connected_callback);
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
    dap_global_db_instance_t *l_dbi = dap_global_db_instance_get_default();
    if (l_dbi) {
        dap_global_db_cluster_t *it = NULL;
        DL_FOREACH(l_dbi->clusters, it) {
            if (it->links_cluster && it->role_cluster) {
                size_t l_links_member_count = dap_stream_cluster_members_count(it->links_cluster);
                size_t l_role_member_count = dap_stream_cluster_members_count(it->role_cluster);
                if ((it->links_cluster->role == DAP_CLUSTER_ROLE_AUTONOMIC || it->links_cluster->role == DAP_CLUSTER_ROLE_ISOLATED) && 
                    l_role_member_count && l_role_member_count != l_links_member_count) {
                    dap_stream_node_addr_t *l_role_members = dap_stream_get_members_addr(it->role_cluster, &l_role_member_count);
                    dap_list_t *l_node_list = it->link_manager->callbacks.get_node_list(it->links_cluster->mnemonim);
                    for (size_t i = 0; i < l_role_member_count; ++i) {
                        if(!dap_cluster_member_find_unsafe(it->links_cluster, l_role_members + i)) {
                            dap_link_t l_link_to_find = { .addr.uint64 = l_role_members[i].uint64 };
                            dap_list_t *l_link_finded = dap_list_find(l_node_list, &l_link_to_find, dap_link_compare);
                            if (l_link_finded) {
                                s_client_connect(l_link_finded->data, "GND");
                            } else {
                                log_it(L_INFO, "Can't find node "NODE_ADDR_FP_STR" in node list %s net", NODE_ADDR_FP_ARGS_S(l_role_members[i]), it->links_cluster->mnemonim);
                            }
                        }
                    }
                    // pthread_mutex_lock(&s_link_manager_links_rwlock);
                    dap_link_t *l_link = NULL, *l_link_tmp = NULL, *l_link_found = NULL;
                    HASH_ITER(hh, l_link_manager->self_links, l_link, l_link_tmp) {
                        if (!dap_stream_find_by_addr(&l_link->addr, NULL)) {
                            s_client_connect(l_link, "GND");
                        }
                    }
                    // pthread_mutex_unlock(&s_link_manager_links_rwlock);
                } else if(it->links_cluster && it->links_cluster->role == DAP_CLUSTER_ROLE_EMBEDDED) {
                    // s_link_manager_update_embeded(l_link_manager);
                }
            }
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
    dap_return_val_if_pass_err(!a_callbacks || !a_callbacks->get_node_list, NULL, "Needed link manager callbacks not filled, please check it");
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

void dap_link_manager_add_active_net(char *a_net_name)
{
    dap_return_if_pass(!s_link_manager || !a_net_name);
    s_link_manager->active_nets = dap_list_append(s_link_manager->active_nets, (void *)a_net_name);
}

void dap_link_manager_remove_active_net(char *a_net_name)
{
    dap_return_if_pass(!s_link_manager || !a_net_name);
    s_link_manager->active_nets = dap_list_remove(s_link_manager->active_nets, (void *)a_net_name);
}

/**
 * @brief dap_chain_node_client_connect
 * Create new dap_client, setup it, and send it in adventure trip
 * @param a_node_client dap_chain_node_client_t
 * @param a_active_channels a_active_channels
 * @return true
 * @return false
 */
bool s_client_connect(dap_link_t *a_link, const char *a_active_channels)
{
// sanity check 
    dap_return_val_if_pass(!a_link || !a_link->net, false); 
//func work
    a_link->client = dap_client_new(s_client_delete_callback, s_client_error_callback, a_link);
    dap_client_set_is_always_reconnect(a_link->client, false);
    a_link->client->_inheritor = a_link;
    dap_client_set_active_channels_unsafe(a_link->client, a_active_channels);

    dap_client_set_auth_cert(a_link->client, a_link->net);

    char l_host_addr[INET6_ADDRSTRLEN] = { '\0' };
    if(a_link->addr_v4.s_addr){
        struct sockaddr_in sa4 = { .sin_family = AF_INET, .sin_addr = a_link->addr_v4 };
        inet_ntop(AF_INET, &(((struct sockaddr_in *) &sa4)->sin_addr), l_host_addr, INET6_ADDRSTRLEN);
    } else {
        struct sockaddr_in6 sa6 = { .sin6_family = AF_INET6, .sin6_addr = a_link->addr_v6 };
        inet_ntop(AF_INET6, &(((struct sockaddr_in6 *) &sa6)->sin6_addr), l_host_addr, INET6_ADDRSTRLEN);
    }
    if(!strlen(l_host_addr) || !strcmp(l_host_addr, "::") || !a_link->port) {
        log_it(L_WARNING, "Undefined address of node client");
        return false;
    }
    log_it(L_INFO, "Connecting to addr %s : %d", l_host_addr, a_link->port);
    dap_client_set_uplink_unsafe(a_link->client, l_host_addr, a_link->port);
    a_link->state = LINK_STATE_CONNECTING;
    // Handshake & connect
    dap_client_go_stage(a_link->client, STAGE_STREAM_STREAMING, s_client_connected_callback);
    return true;
}

int dap_link_manager_link_add(dap_link_t *a_link)
{
// sanity check
    dap_return_val_if_pass(!a_link, -1);
// func work
    // pthread_mutex_lock(&s_link_manager_links_rwlock);
    if (HASH_COUNT(a_link->link_manager->alien_links) >= 1000) {
        // pthread_mutex_unlock(&s_link_manager_links_rwlock);
        return 1;
    }
    // uint64_t l_own_addr = dap_chain_net_get_cur_addr_int(a_net);
    // if (a_link_node_info->hdr.address.uint64 == l_own_addr) {
    //     // pthread_mutex_unlock(&s_link_manager_links_rwlock);
    //     return -2;
    // }
    uint64_t l_addr = a_link->addr_v4.s_addr;
    dap_link_t *l_new_link = NULL;
    HASH_FIND(hh, a_link->link_manager->alien_links, &l_addr, sizeof(l_addr), l_new_link);
    if (!l_new_link)
        HASH_FIND(hh, a_link->link_manager->self_links, &l_addr, sizeof(l_addr), l_new_link);
    if (l_new_link) {
        // pthread_mutex_unlock(&s_link_manager_links_rwlock);
        return -3;
    }
    l_new_link = DAP_NEW_Z(dap_link_t);
    if (!l_new_link) {
        log_it(L_CRITICAL, "Memory allocation error");
        // pthread_mutex_unlock(&s_link_manager_links_rwlock);
        return -4;
    }
    // l_new_link->info = DAP_DUP(a_link_node_info);
    // l_new_link->uplink_ip = a_link_node_info->hdr.ext_addr_v4.s_addr;
    // l_new_link->net = a_net;
    HASH_ADD(hh, a_link->link_manager->alien_links, uplink_ip, sizeof(l_new_link->uplink_ip), l_new_link);
    // pthread_mutex_unlock(&s_link_manager_links_rwlock);
    return 0;
}

int dap_link_compare(dap_list_t *a_list1, dap_list_t *a_list2)
{
    dap_return_val_if_pass(!a_list1 || !a_list2, -1);
    return !(((dap_link_t *)(a_list1->data))->addr.uint64 == ((dap_link_t *)(a_list2->data))->addr.uint64);
}