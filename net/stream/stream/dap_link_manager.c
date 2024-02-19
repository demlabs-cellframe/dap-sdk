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
#include "dap_strfuncs.h"

#define LOG_TAG "dap_link_manager"

typedef struct dap_managed_net {
    bool active;
    int32_t links_count;
    uint64_t id;
    dap_cluster_t *node_link_cluster;
} dap_managed_net_t;

static uint32_t s_timer_update_states = 4000;
static uint32_t s_min_links_num = 5;
static dap_link_manager_t *s_link_manager = NULL;

static void s_client_connect(dap_link_t *a_link, const char *a_active_channels, void *a_callback_arg);
static void s_client_connected_callback(dap_client_t *a_client, void *a_arg);
static void s_client_error_callback(dap_client_t *a_client, void *a_arg);
static void s_client_delete_callback(UNUSED_ARG dap_client_t *a_client, void *a_arg);
static bool s_check_active_nets();

/**
 * @brief dap_chain_node_client_connect
 * Create new dap_client, setup it, and send it in adventure trip
 * @param a_node_client dap_chain_node_client_t
 * @param a_active_channels a_active_channels
 * @param a_link_cluster - cluster to added node addr if connected
 */
void s_client_connect(dap_link_t *a_link, const char *a_active_channels, void *a_callback_arg)
{
// sanity check 
    dap_return_if_pass(!a_link); 
//func work
    a_link->client = dap_client_new(s_client_delete_callback, s_client_error_callback, a_callback_arg);
    dap_client_set_is_always_reconnect(a_link->client, false);
    a_link->client->_inheritor = a_link;
    dap_client_set_active_channels_unsafe(a_link->client, a_active_channels);
    log_it(L_INFO, "Connecting to addr %s : %d", a_link->host_addr_str, a_link->host_port);
    dap_client_set_uplink_unsafe(a_link->client, a_link->host_addr_str, a_link->host_port);
    a_link->state = LINK_STATE_CONNECTING;
    // Handshake & connect
    dap_client_go_stage(a_link->client, STAGE_STREAM_STREAMING, s_client_connected_callback);
    return;
}

/**
 * @brief s_client_connected_callback
 * @param a_client
 * @param a_arg
 */
static void s_client_connected_callback(dap_client_t *a_client, void *a_arg)
{
// sanity check
    dap_return_if_pass(!DAP_LINK(a_client));
// func work
    dap_link_t *l_link = DAP_LINK(a_client);
    dap_list_t *l_item = NULL;
    DL_FOREACH(l_link->links_clusters, l_item) {
        dap_cluster_member_add((dap_cluster_t *)l_item->data, &l_link->node_addr, 0, NULL);
    }
    // if dynamic link, increment counter and call callback
    if (a_arg) {
        dap_managed_net_t *l_net = (dap_managed_net_t *)a_arg;
        dap_cluster_member_add(l_net->node_link_cluster, &l_link->node_addr, 0, NULL);
        if(l_link->link_manager->callbacks.connected)
            l_link->link_manager->callbacks.connected(l_link, l_net->id);
    }
    log_it(L_NOTICE, "Stream connection with node "NODE_ADDR_FP_STR" (%s:%hu) established",
                NODE_ADDR_FP_ARGS_S(l_link->node_addr),
                l_link->host_addr_str, l_link->host_port);

    l_link->state = LINK_STATE_ESTABLISHED;
}


/**
 * @brief s_client_error_callback
 * @param a_client
 * @param a_arg
 */
void s_client_error_callback(dap_client_t *a_client, void *a_arg)
{
// sanity check
    dap_return_if_pass(!DAP_LINK(a_client));
// func work
    dap_link_t *l_link = DAP_LINK(a_client);
    // check for last attempt
    bool l_is_last_attempt = a_arg ? true : false;
    uint64_t l_net_id = a_client->callbacks_arg ? ((dap_managed_net_t *)(a_client->callbacks_arg))->id : 0;
    if (l_is_last_attempt) {
        l_link->state = LINK_STATE_DISCONNECTED;

        if (l_link->link_manager->callbacks.disconnected) {
            l_link->link_manager->callbacks.disconnected(l_link, l_net_id, ((dap_managed_net_t *)(a_client->callbacks_arg))->links_count );
        }
        if (l_link->keep_connection) {
            if (dap_client_get_stage(l_link->client) != STAGE_BEGIN)
                dap_client_go_stage(l_link->client, STAGE_BEGIN, NULL);
            log_it(L_INFO, "Reconnecting node client with peer "NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS_S(l_link->node_addr));
            l_link->state = LINK_STATE_CONNECTING ;
            dap_client_go_stage(l_link->client, STAGE_STREAM_STREAMING, s_client_connected_callback);
        } else {
            dap_cluster_link_delete_from_all(&l_link->node_addr);
        }
    } else if(l_link->link_manager->callbacks.error) // TODO make different error codes
        l_link->link_manager->callbacks.error(l_link, l_net_id, EINVAL);
}

void s_client_delete_callback(UNUSED_ARG dap_client_t *a_client, void *a_arg)
{
    // TODO make decision for possible client replacement
    assert(a_arg);
    dap_chain_node_client_close_unsafe(a_arg);
}

bool s_update_states(void *a_arg)
{
// sanity check
    dap_link_manager_t *l_link_manager = (dap_link_manager_t *)a_arg;
    dap_return_val_if_pass(!l_link_manager, true);
// func work
    if(!l_link_manager->active || !s_check_active_nets()) {
        return true;
    }
    // dynamic link work
    dap_list_t *l_item = NULL;
    DL_FOREACH(s_link_manager->nets, l_item) {
        dap_managed_net_t *l_net = (dap_managed_net_t *)l_item->data;
        if(l_net->active && l_link_manager->callbacks.link_request && l_net->links_count < l_link_manager->min_links_num)
            l_link_manager->callbacks.link_request(l_net->id);
    }
    // static link work
    dap_link_t *l_link = NULL, *l_tmp = NULL;
    // pthread_rwlock_wrlock(&a_cluster->members_lock);
    HASH_ITER(hh, l_link_manager->links, l_link, l_tmp) {
        // if we don't have any connections with members in role clusters then create connection
        if(!l_link->client && l_link->role_clusters) {
            if (!l_link_manager->callbacks.fill_net_info(l_link)) {
                s_client_connect(l_link, "CGND", NULL);
            } else {
                log_it(L_INFO, "Can't find node "NODE_ADDR_FP_STR" in node list", NODE_ADDR_FP_ARGS_S(l_link->node_addr));
            }
        } else if (l_link->client && !l_link->role_clusters) {
            // recheck dynamic cluster and if no need close connect
        }
    }
    return true;
}

bool s_check_active_nets()
{
// sanity check
    dap_return_val_if_pass(!s_link_manager, false);
// func work
    dap_list_t *l_item = NULL;
    DL_FOREACH(s_link_manager->nets, l_item) {
        if (((dap_managed_net_t *)(l_item->data))->active)
            return true;
    }
    return false;
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
    dap_list_free(s_link_manager->nets);  // TODO fool memry free
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
    if(!l_ret->callbacks.link_request)
        log_it(L_WARNING, "Link manager link_request callback is NULL");
    l_ret->min_links_num = s_min_links_num;
    l_ret->active = true;
    return l_ret;
}

DAP_INLINE dap_link_manager_t *dap_link_manager_get_default()
{
    return s_link_manager;
}

size_t dap_link_manager_links_count(uint64_t a_net_id)
{
// sanity check
    dap_return_val_if_pass(!s_link_manager, 0);
// func work
    dap_list_t *l_item = NULL;
    size_t l_ret = 0;
    DL_FOREACH(s_link_manager->nets, l_item) {
        if (a_net_id == ((dap_managed_net_t *)(l_item->data))->id) {
            l_ret = ((dap_managed_net_t *)(l_item->data))->links_count;
            break;
        }
    }
    return l_ret;
}


size_t dap_link_manager_needed_links_count(uint64_t a_net_id)
{
// sanity check
    dap_return_val_if_pass(!s_link_manager, 0);
// func work
    dap_list_t *l_item = NULL;
    DL_FOREACH(s_link_manager->nets, l_item) {
        dap_managed_net_t *l_net = (dap_managed_net_t *)l_item->data;
        if (a_net_id == l_net->id && l_net->links_count < s_link_manager->min_links_num) {
            return s_link_manager->min_links_num - l_net->links_count;
        }
    }
    return 0;
}

int dap_link_manager_add_net(uint64_t a_net_id, dap_cluster_t *a_link_cluster)
{
    dap_return_val_if_pass(!s_link_manager || !a_net_id, -2);
    dap_managed_net_t *l_net = NULL;
    DAP_NEW_Z_RET_VAL(l_net, dap_managed_net_t, -3, NULL);
    l_net->id = a_net_id;
    l_net->node_link_cluster = a_link_cluster;
    s_link_manager->nets = dap_list_append(s_link_manager->nets, (void *)l_net);
    return 0;
}


void dap_link_manager_remove_net(uint64_t a_net_id)
{
    dap_return_if_pass(!s_link_manager || !a_net_id);
    dap_list_t *l_item = NULL;
    DL_FOREACH(s_link_manager->nets, l_item) {
        if (a_net_id == ((dap_managed_net_t *)(l_item->data))->id) {
            s_link_manager->nets = dap_list_remove_link(s_link_manager->nets, l_item);
            break;
        }
    }
    if (!l_item) {
        log_it(L_ERROR, "Net ID %zu not controlled by link manager", a_net_id);
        return;
    }
    // TODO write func compare controlled nets struct
    DAP_DEL_MULTY(l_item->data, l_item);
}

void dap_link_manager_set_net_status(uint64_t a_net_id, bool a_status)
{
    dap_return_if_pass(!s_link_manager || !a_net_id);
    dap_list_t *l_item = NULL;
    DL_FOREACH(s_link_manager->nets, l_item) {
        if (a_net_id == ((dap_managed_net_t *)(l_item->data))->id) {
            ((dap_managed_net_t *)(l_item->data))->active = a_status;
            break;
        }
    }
    if (!l_item) {
        log_it(L_ERROR, "Net %zu not controlled by link manager", a_net_id);
    }
}

void dap_link_manager_add_role_cluster(dap_stream_node_addr_t *a_addr, dap_cluster_t *a_cluster)
{
    dap_return_if_pass(!s_link_manager || !a_addr || !a_cluster);
    dap_link_t *l_link = dap_link_manager_link_create_or_update(a_addr, NULL, NULL, 0);
    l_link->role_clusters = dap_list_append(l_link->role_clusters, a_cluster);
}

void dap_link_manager_add_links_cluster(dap_stream_node_addr_t *a_addr, dap_cluster_t *a_cluster)
{
    dap_return_if_pass(!s_link_manager || !a_addr || !a_cluster);
    dap_link_t *l_link = dap_link_manager_link_create_or_update(a_addr, NULL, NULL, 0);
    l_link->links_clusters = dap_list_append(l_link->links_clusters, a_cluster);
    dap_list_t *l_item = NULL;
    if (a_cluster->role == DAP_CLUSTER_ROLE_EMBEDDED)
        DL_FOREACH(s_link_manager->nets, l_item) {
            dap_managed_net_t *l_net = (dap_managed_net_t *)l_item->data;
            if (!strcmp((l_net->node_link_cluster->mnemonim), a_cluster->mnemonim)) {
                l_net->links_count++;
                break;
            }
        }
}

void dap_link_manager_remove_role_cluster(dap_stream_node_addr_t *a_addr, dap_cluster_t *a_cluster)
{
    dap_return_if_pass(!s_link_manager || !a_addr || !a_cluster);
    // pthread_rwlock_wrlock(&it->members_lock);
    dap_link_t *l_link = NULL;
    HASH_FIND(hh, s_link_manager->links, a_addr, sizeof(*a_addr), l_link);
    if (!l_link) {
        log_it(L_ERROR, "Try cluster deleting from non-existent link");
        // pthread_rwlock_unlock(&it->members_lock);
        return;
    }
    l_link->role_clusters = dap_list_remove(l_link->role_clusters, a_cluster);
    // pthread_rwlock_unlock(&it->members_lock);
}

void dap_link_manager_remove_links_cluster(dap_stream_node_addr_t *a_addr, dap_cluster_t *a_cluster)
{
    dap_return_if_pass(!s_link_manager || !a_addr || !a_cluster);
    // pthread_rwlock_wrlock(&it->members_lock);
    dap_link_t *l_link = NULL;
    HASH_FIND(hh, s_link_manager->links, a_addr, sizeof(*a_addr), l_link);
    if (!l_link) {
        log_it(L_ERROR, "Try cluster deleting from non-existent link");
        // pthread_rwlock_unlock(&it->members_lock);
        return;
    }
    l_link->links_clusters = dap_list_remove(l_link->links_clusters, a_cluster);
    dap_list_t *l_item = NULL;
    if (a_cluster->role == DAP_CLUSTER_ROLE_EMBEDDED)
        DL_FOREACH(s_link_manager->nets, l_item) {
            dap_managed_net_t *l_net = (dap_managed_net_t *)l_item->data;
            if (!strcmp((l_net->node_link_cluster->mnemonim), a_cluster->mnemonim)) {
                l_net->links_count--;
                break;
            }
        }
    // pthread_rwlock_unlock(&it->members_lock);
}

dap_link_t *dap_link_manager_link_create_or_update(dap_stream_node_addr_t *a_node_addr, 
    struct in_addr *a_addr_v4, struct in6_addr *a_addr_v6, uint16_t a_port)
{
// sanity check
    dap_return_val_if_pass(!a_node_addr || !a_node_addr->uint64, NULL);
// func work
    dap_link_t *l_ret = NULL;
    // pthread_rwlock_wrlock(&a_cluster->members_lock);
    HASH_FIND(hh, s_link_manager->links, a_node_addr, sizeof(*a_node_addr), l_ret);
    if (!l_ret) {
        DAP_NEW_Z_RET_VAL(l_ret, dap_link_t, NULL, NULL);
        l_ret->node_addr.uint64 = a_node_addr->uint64;
        l_ret->link_manager = s_link_manager;
        HASH_ADD(hh, s_link_manager->links, node_addr, sizeof(l_ret->node_addr), l_ret);
    }
    // pthread_rwlock_unlock(&a_cluster->members_lock);

    // fill addr
    if(a_addr_v4 && a_addr_v4->s_addr){
        inet_ntop(AF_INET, a_addr_v4, l_ret->host_addr_str, INET_ADDRSTRLEN);
    } else if (a_addr_v6) {
        inet_ntop(AF_INET6, a_addr_v6, l_ret->host_addr_str, INET6_ADDRSTRLEN);
    }
    if (a_port)
    l_ret->host_port = a_port;
    l_ret->host_port = a_port;
    l_ret->node_addr.uint64 = a_node_addr->uint64;
        l_ret->host_port = a_port;
    l_ret->node_addr.uint64 = a_node_addr->uint64;
    return l_ret;
}

int dap_link_manager_link_add(uint64_t a_net_id, dap_link_t *a_link)
{
// sanity check
    dap_return_val_if_pass(!a_net_id || !a_link || !s_link_manager->active || !s_check_active_nets(), -1);
// func work
    dap_list_t *l_item = NULL;
    DL_FOREACH(s_link_manager->nets, l_item) {
        if (a_net_id == ((dap_managed_net_t *)(l_item->data))->id) {
            break;
        }
    }
    if (!l_item) {
        log_it(L_ERROR, "Can't find %zu netowrk ID in link manager list");
        return -2;
    }

    dap_link_t *l_link = NULL;
    // pthread_rwlock_wrlock(&a_cluster->members_lock);
    HASH_FIND(hh, s_link_manager->links, &a_link->node_addr, sizeof(a_link->node_addr), l_link);
    // pthread_rwlock_unlock(&a_cluster->members_lock);

    if (l_link != a_link) {
        log_it(L_WARNING, "LEAKS, links dublicate to node "NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS_S(a_link->node_addr));
        return -3;
    }

    if (!l_link || !l_link->client) {
        s_client_connect(a_link, "CGND", l_item->data);
    } else {
        log_it(L_INFO, "Use existed link to "NODE_ADDR_FP_STR" %s:%hu", NODE_ADDR_FP_ARGS_S(l_link->node_addr), l_link->host_addr_str, l_link->host_port);
        dap_cluster_member_add(((dap_managed_net_t *)(l_item->data))->node_link_cluster, &l_link->node_addr, 0, NULL);
        if(l_link->link_manager->callbacks.connected)
            l_link->link_manager->callbacks.connected(l_link, ((dap_managed_net_t *)(l_item->data))->id);
        return 0;
    }
    return 0;
}