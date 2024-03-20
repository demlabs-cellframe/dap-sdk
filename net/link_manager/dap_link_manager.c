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

#define DAP_LINK(a) ((dap_link_t *)(a)->_inheritor)

typedef struct dap_managed_net {
    bool active;
    int32_t links_count;
    uint64_t id;
    dap_cluster_t *node_link_cluster;
} dap_managed_net_t;

static bool s_debug_more = false;
static const char *s_init_error = "Link manager not inited";
static uint32_t s_timer_update_states = 4000;
static uint32_t s_min_links_num = 5;
static uint32_t s_max_attempts_num = 2;
static dap_link_manager_t *s_link_manager = NULL;

static void s_client_connect(dap_link_t *a_link, void *a_callback_arg);
static void s_client_connected_callback(dap_client_t *a_client, void *a_arg);
static void s_client_error_callback(dap_client_t *a_client, void *a_arg);
static void s_client_delete_callback(UNUSED_ARG dap_client_t *a_client, void *a_arg);
static void s_accounting_uplink_in_net(dap_link_t *a_link, dap_managed_net_t *a_net);
static void s_link_delete(dap_link_t *a_link, bool a_force);
static void s_link_delete_all(bool a_force);
static bool s_check_active_nets();
static void s_links_wake_up();
static void s_links_request();
static bool s_update_states(void *a_arg);
static void s_link_manager_print_links_info();
static dap_list_t *s_find_net_item_by_id(uint64_t a_net_id)
{
    dap_return_val_if_pass_err(!s_link_manager, NULL, s_init_error);
    dap_return_val_if_pass(!a_net_id, NULL);
    dap_list_t *l_item = NULL;
    DL_FOREACH(s_link_manager->nets, l_item)
        if (a_net_id == ((dap_managed_net_t *)(l_item->data))->id)
            break;
    if (!l_item) {
        log_it(L_ERROR, "Net ID %"DAP_UINT64_FORMAT_U" not controlled by link manager", a_net_id);
        return NULL;
    }
    return l_item;
}
DAP_STATIC_INLINE dap_managed_net_t *s_find_net_by_id(uint64_t a_net_id)
{
    dap_list_t *l_item = s_find_net_item_by_id(a_net_id);
    return l_item ? (dap_managed_net_t *)l_item->data : NULL;
}
// debug_more funcs
DAP_STATIC_INLINE void s_debug_cluster_adding_removing(bool a_static, bool a_adding, dap_cluster_t *a_cluster, dap_stream_node_addr_t *a_node_addr)
{
    debug_if(s_debug_more, L_DEBUG, "%s cluster net_id %"DAP_UINT64_FORMAT_U", svc_id %"DAP_UINT64_FORMAT_U" successfully %s link "NODE_ADDR_FP_STR,
            a_static ? "Static" : "Links", 
            a_cluster->uuid.net_id,
            a_cluster->uuid.svc_id,
            a_adding ? "added to" : "removed from",
            NODE_ADDR_FP_ARGS(a_node_addr));
}
DAP_STATIC_INLINE void s_debug_accounting_link_in_net(bool a_uplink, dap_stream_node_addr_t *a_node_addr, uint64_t a_net_id)
{
    debug_if(s_debug_more, L_DEBUG, "Accounting %slink from "NODE_ADDR_FP_STR" in net %"DAP_UINT64_FORMAT_U,
            a_uplink ? "up" : "down", NODE_ADDR_FP_ARGS(a_node_addr), a_net_id);
}
DAP_STATIC_INLINE void s_debug_counting_links_in_net(bool a_inc, dap_managed_net_t *a_net)
{
    debug_if(s_debug_more, a_net->links_count < 0 ? L_ERROR : L_DEBUG, "Links counter in net %"DAP_UINT64_FORMAT_U" was %scremented and equal %d",
            a_net->id, a_inc ? "in" : "de", a_net->links_count);
}

/**
 * @brief dap_chain_node_client_connect
 * Create new dap_client, setup it, and send it in adventure trip
 * @param a_node_client dap_chain_node_client_t
 * @param a_active_channels a_active_channels
 * @param a_link_cluster - cluster to added node addr if connected
 */
void s_client_connect(dap_link_t *a_link, void *a_callback_arg)
{
// sanity check 
    dap_return_if_pass(!a_link || !a_link->valid || !a_link->client);
//func work
    if (a_link->state == LINK_STATE_DISCONNECTED) {
        a_link->client->callbacks_arg = a_callback_arg;
        if (dap_client_get_stage(a_link->client) != STAGE_BEGIN) {
            dap_client_go_stage(a_link->client, STAGE_BEGIN, NULL);
        }
        log_it(L_INFO, "Connecting to node " NODE_ADDR_FP_STR ", addr %s : %d", NODE_ADDR_FP_ARGS_S(a_link->client->link_info.node_addr), a_link->client->link_info.uplink_addr, a_link->client->link_info.uplink_port);
        a_link->state = LINK_STATE_CONNECTING ;
        dap_client_go_stage(a_link->client, STAGE_STREAM_STREAMING, s_client_connected_callback);
    } else if (a_callback_arg && a_link->state == LINK_STATE_ESTABLISHED) {
        s_accounting_uplink_in_net(a_link, (dap_managed_net_t *)a_callback_arg);
    }
    return;
}

/**
 * @brief s_client_connected_callback
 * @param a_client - client to connect
 * @param a_arg - callback args, pointer dap_managed_net_t
 */
void s_client_connected_callback(dap_client_t *a_client, void *a_arg)
{
// sanity check
    dap_return_if_pass(!a_client || !DAP_LINK(a_client) );
    dap_link_t *l_link = DAP_LINK(a_client);
// func work
    dap_list_t *l_item = NULL;
    DL_FOREACH(l_link->static_links_clusters, l_item) {
        dap_cluster_member_add((dap_cluster_t *)l_item->data, &l_link->client->link_info.node_addr, 0, NULL);
    }
    // if dynamic link, add net cluster to list and call callback
    dap_managed_net_t *l_net = (dap_managed_net_t *)a_arg;
    if (l_net && l_net->active) {
        dap_cluster_member_add(l_net->node_link_cluster, &l_link->client->link_info.node_addr, 0, NULL);
        if(l_link->link_manager->callbacks.connected)
            l_link->link_manager->callbacks.connected(l_link, l_net->id);
    }
    log_it(L_NOTICE, "Stream connection with node "NODE_ADDR_FP_STR" (%s:%hu) established",
                NODE_ADDR_FP_ARGS_S(l_link->client->link_info.node_addr),
                l_link->client->link_info.uplink_addr, l_link->client->link_info.uplink_port);
    l_link->attempts_count = 0;
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
    dap_return_if_pass(!a_client || !DAP_LINK(a_client));
    dap_link_t *l_link = DAP_LINK(a_client);
// func work
    // check for last attempt
    bool l_is_last_attempt = a_arg ? true : false;
    uint64_t l_net_id = a_client->callbacks_arg ? ((dap_managed_net_t *)(a_client->callbacks_arg))->id : 0;
    if (l_is_last_attempt) {
        l_link->state = LINK_STATE_DISCONNECTED;

        if (l_link->link_manager->callbacks.disconnected && l_net_id) {
            l_link->link_manager->callbacks.disconnected(l_link, l_net_id, ((dap_managed_net_t *)(a_client->callbacks_arg))->links_count);
        }
        // remove node addr from all links clusters
        dap_list_t *l_item = NULL;
        DL_FOREACH(l_link->links_clusters, l_item) {
            dap_cluster_member_delete(l_item->data, &l_link->client->link_info.node_addr);
        }
    } else if(l_link->link_manager->callbacks.error) // TODO make different error codes
        l_link->link_manager->callbacks.error(l_link, l_net_id, EINVAL);
}

/**
 * @brief s_client_delete_callback
 * @param a_client
 * @param a_arg
 */
void s_client_delete_callback(dap_client_t *a_client, void *a_arg)
{
// sanity check
    dap_return_if_pass(!a_client || !DAP_LINK(a_client));
    dap_link_t *l_link = DAP_LINK(a_client);
// func work
    l_link->client = NULL;
}

/**
 * @brief used to add member to link cluster if stream established
 * @param a_link - link to check
 * @param a_net - net to check
 */
void s_accounting_uplink_in_net(dap_link_t *a_link, dap_managed_net_t *a_net)
{
// sanity check
    dap_return_if_pass(!a_link || !a_net);
// func work
    if (!dap_cluster_member_find_unsafe(a_net->node_link_cluster, &a_link->client->link_info.node_addr)) {
        dap_cluster_member_add(a_net->node_link_cluster, &a_link->client->link_info.node_addr, 0, NULL);
        if(a_link->link_manager->callbacks.connected)
            a_link->link_manager->callbacks.connected(a_link, a_net->id);
        s_debug_accounting_link_in_net(true, &a_link->client->link_info.node_addr, a_net->id);
    }
}

/**
 * @brief Memory free from link !!!hash table should be locked!!!
 * @param a_link - link to delet
 * @param a_force - only del dynamic, if true - all links types memory free
 */
void s_link_delete(dap_link_t *a_link, bool a_force)
{
// sanity check
    dap_return_if_pass(!a_link);
// func work
    debug_if(s_debug_more, L_DEBUG, "%seleting link to node " NODE_ADDR_FP_STR "", a_force ? "Force d" : "D", NODE_ADDR_FP_ARGS_S(a_link->client->link_info.node_addr));
    if(a_link->static_links_clusters && !a_force)
        return;
    a_link->state = LINK_STATE_DELETED;
    dap_client_go_stage(a_link->client, STAGE_BEGIN, NULL);
    dap_client_delete_mt(a_link->client);
    dap_list_free(a_link->static_links_clusters);
    HASH_DEL(s_link_manager->links, a_link);
    DAP_DELETE(a_link);
}

/**
 * @brief Memory free from all links in link manager
 * @param a_force - only del dynamic, if true - all links types memory free
 */
void s_link_delete_all(bool a_force)
{
// sanity check
    dap_return_if_pass(!s_link_manager);
// func work
    dap_link_t *l_link = NULL, *l_tmp = NULL;
    pthread_rwlock_wrlock(&s_link_manager->links_lock);
        HASH_ITER(hh, s_link_manager->links, l_link, l_tmp) {
            s_link_delete(l_link, a_force);
        }
    pthread_rwlock_unlock(&s_link_manager->links_lock);
}


/**
 * @brief Check existed links
 */
void s_links_wake_up()
{
    dap_link_t *l_link = NULL, *l_tmp = NULL;
    pthread_rwlock_rdlock(&s_link_manager->links_lock);
        bool l_active_nets = s_check_active_nets();
        HASH_ITER(hh, s_link_manager->links, l_link, l_tmp) {
            // if disconnected try connect
            if(l_link->state == LINK_STATE_DISCONNECTED  && (l_link->static_links_clusters || l_active_nets)) {
                if (l_link->attempts_count >= s_link_manager->max_attempts_num) {
                    s_link_delete(l_link, false);
                } else if (l_link->valid || !s_link_manager->callbacks.fill_net_info(l_link)) {
                    s_client_connect(l_link, l_link->client->callbacks_arg);
                } else {
                    log_it(L_INFO, "Can't find node "NODE_ADDR_FP_STR" in node list", NODE_ADDR_FP_ARGS_S(l_link->client->link_info.node_addr));
                }
                if (!l_link->static_links_clusters)
                    l_link->attempts_count++;
            }
        }
    pthread_rwlock_unlock(&s_link_manager->links_lock);
}

/**
 * @brief Create request to new links
 */
void s_links_request()
{
// func work
    // dynamic link work
    dap_list_t *l_item = NULL;
    DL_FOREACH(s_link_manager->nets, l_item) {
        dap_managed_net_t *l_net = (dap_managed_net_t *)l_item->data;
        if(l_net->active && s_link_manager->callbacks.link_request && l_net->links_count < s_link_manager->min_links_num)
            s_link_manager->callbacks.link_request(l_net->id);
    }
}

/**
 * @brief serially call funcs s_links_wake_up and s_links_request
 * @param a_arg UNUSED
 * @return false if error or manager inactiove, other - true
 */
bool s_update_states(UNUSED_ARG void *a_arg)
{
// sanity check
    dap_return_val_if_pass_err(!s_link_manager, false, s_init_error);
    if (s_debug_more)
        s_link_manager_print_links_info();
    // if inactive remove timer
    if (!s_link_manager->active) {
        s_link_manager->update_timer = NULL;
        return false;
    }
    // static mode switcher
    static bool l_wakeup_mode = false;
    if (l_wakeup_mode)
        s_links_wake_up();
    else
        s_links_request();
    l_wakeup_mode = !l_wakeup_mode;
    return true;
}

/**
 * @brief check if any network have not offline status
 * @return false if all netorks offline
 */
bool s_check_active_nets()
{
// sanity check
    dap_return_val_if_pass_err(!s_link_manager, false, s_init_error);
// func work
    dap_list_t *l_item = NULL;
    DL_FOREACH(s_link_manager->nets, l_item) {
        if (((dap_managed_net_t *)(l_item->data))->active)
            return true;
    }
    return false;
}

/**
 * @brief link manager initialisation
 * @param a_callbacks - callbacks
 * @return 0 if ok, other if error
 */
int dap_link_manager_init(const dap_link_manager_callbacks_t *a_callbacks)
{
// sanity check
    dap_return_val_if_pass_err(s_link_manager, -2, "Link manager actualy inited");
// func work
    s_timer_update_states = dap_config_get_item_uint32_default(g_config, "link_manager", "timer_update_states", s_timer_update_states);
    s_min_links_num = dap_config_get_item_uint32_default(g_config, "link_manager", "min_links_num", s_min_links_num);
    s_max_attempts_num = dap_config_get_item_uint32_default(g_config, "link_manager", "max_attempts_num", s_max_attempts_num);
    s_debug_more = dap_config_get_item_bool_default(g_config,"link_manager","debug_more", s_debug_more);
    if (!(s_link_manager = dap_link_manager_new(a_callbacks))) {
        log_it(L_ERROR, "Default link manager not inited");
        return -1;
    }
    dap_link_manager_set_condition(true);
    return 0;
}

/**
 * @brief close connections and memory free
 */
void dap_link_manager_deinit()
{
// sanity check
    dap_return_if_pass_err(!s_link_manager, s_init_error);
// func work
    dap_link_manager_set_condition(false);
    s_link_delete_all(true);
    dap_list_free(s_link_manager->nets);  // TODO fool memry free
    DAP_DELETE(s_link_manager);
}

/**
 * @brief close connections and memory free
 * @param a_callbacks - callbacks
 * @return pointer to dap_link_manager_t, NULL if error
 */
dap_link_manager_t *dap_link_manager_new(const dap_link_manager_callbacks_t *a_callbacks)
{
// sanity check
    dap_return_val_if_pass_err(!a_callbacks || !a_callbacks->fill_net_info, NULL, "Needed link manager callbacks not filled, please check it");
// memory alloc
    dap_link_manager_t *l_ret = NULL;
    DAP_NEW_Z_RET_VAL(l_ret, dap_link_manager_t, NULL, NULL);
// func work
    l_ret->callbacks = *a_callbacks;
    if(!l_ret->callbacks.link_request)
        log_it(L_WARNING, "Link manager link_request callback is NULL");
    l_ret->min_links_num = s_min_links_num;
    l_ret->max_attempts_num = s_max_attempts_num;
    pthread_rwlock_init(&l_ret->links_lock, NULL);
    return l_ret;
}

/**
 * @brief dap_link_manager_get_default
 * @return pointer to s_link_manager
 */
DAP_INLINE dap_link_manager_t *dap_link_manager_get_default()
{
    return s_link_manager;
}

/**
 * @brief count links in concretic net
 * @param a_net_id net id for search
 * @return links count
 */
size_t dap_link_manager_links_count(uint64_t a_net_id)
{
// sanity check
    dap_managed_net_t *l_net = s_find_net_by_id(a_net_id);
    dap_return_val_if_pass(!l_net, 0);
// func work
    return l_net->links_count;
}

/**
 * @brief count needed links in concretic net
 * @param a_net_id net id for search
 * @return needed links count
 */
size_t dap_link_manager_needed_links_count(uint64_t a_net_id)
{
// sanity check
    dap_return_val_if_pass(!s_link_manager, 0);
// func work
    int32_t l_links_count = dap_link_manager_links_count(a_net_id);
    if (l_links_count < s_link_manager->min_links_num)
        return s_link_manager->min_links_num - l_links_count;
    return 0;
}

/**
 * @brief add controlled net to link manager 
 * @param a_net_id net id for adding
 * @param a_link_cluster net link cluster for adding
 * @return 0 if ok, other - ERROR
 */
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

/**
 * @brief remove net from managed list
 * @param a_net_id - net id to del
 */
void dap_link_manager_remove_net(uint64_t a_net_id)
{
// sanity check
    dap_list_t *l_net_item = s_find_net_item_by_id(a_net_id);
    dap_return_if_pass(!l_net_item);
// func work
    s_link_manager->nets = dap_list_remove_link(s_link_manager->nets, l_net_item);
    DAP_DEL_MULTY(l_net_item->data, l_net_item);
}

/**
 * @brief set active or inactive status
 * @param a_net_id - net id to set
 */
void dap_link_manager_set_net_condition(uint64_t a_net_id, bool a_new_condition)
{
// sanity check
    dap_managed_net_t *l_net = s_find_net_by_id(a_net_id);
    dap_return_if_pass(!l_net);
// func work
    if (l_net->active && !a_new_condition) {
        dap_cluster_delete_all_members(l_net->node_link_cluster);
    }
    l_net->active = a_new_condition;
}

/**
 * @brief check adding member in links cluster
 * @param a_addr - node addr to adding
 * @param a_cluster - pointer to links cluster
 */
void dap_link_manager_add_links_cluster(dap_stream_node_addr_t *a_addr, dap_cluster_t *a_cluster)
{
// sanity check
    dap_link_t *l_link = dap_link_manager_link_create(a_addr);
    dap_return_if_pass(!l_link || !a_cluster);
// func work
    l_link->links_clusters = dap_list_append(l_link->links_clusters, a_cluster);
    s_debug_cluster_adding_removing(false, true, a_cluster, &l_link->client->link_info.node_addr);
    dap_list_t *l_item = NULL;
    if (a_cluster->role == DAP_CLUSTER_ROLE_EMBEDDED)
        DL_FOREACH(s_link_manager->nets, l_item) {
            dap_managed_net_t *l_net = (dap_managed_net_t *)l_item->data;
            if (!strcmp((l_net->node_link_cluster->mnemonim), a_cluster->mnemonim)) {
                l_net->links_count++;
                s_debug_counting_links_in_net(true, l_net);
                break;
            }
        }
}

/**
 * @brief check removing member from links cluster
 * @param a_addr - node addr to adding
 * @param a_cluster - pointer to links cluster
 */
void dap_link_manager_remove_links_cluster(dap_stream_node_addr_t *a_addr, dap_cluster_t *a_cluster)
{
    dap_return_if_pass(!s_link_manager || !a_addr || !a_cluster);
    pthread_rwlock_rdlock(&s_link_manager->links_lock);
        dap_link_t *l_link = NULL;
        HASH_FIND(hh, s_link_manager->links, a_addr, sizeof(*a_addr), l_link);
        if (!l_link) {
            log_it(L_ERROR, "Try cluster deleting from non-existent link");
            pthread_rwlock_unlock(&s_link_manager->links_lock);
            return;
        }
        l_link->links_clusters = dap_list_remove(l_link->links_clusters, a_cluster);
        s_debug_cluster_adding_removing(false, false, a_cluster, a_addr);
        dap_list_t *l_item = NULL;
        if (a_cluster->role == DAP_CLUSTER_ROLE_EMBEDDED) {
            DL_FOREACH(s_link_manager->nets, l_item) {
                dap_managed_net_t *l_net = (dap_managed_net_t *)l_item->data;
                if (!strcmp((l_net->node_link_cluster->mnemonim), a_cluster->mnemonim)) {
                    l_net->links_count--;
                    s_debug_counting_links_in_net(false, l_net);
                    break;
                }
            }
        }
    pthread_rwlock_unlock(&s_link_manager->links_lock);
}

/**
 * @brief create or update link, if any arg NULL - it's not updated
 * @param a_node_addr - node addr to adding
 * @return if ERROR null, other - pointer to dap_link_t
 */
dap_link_t *dap_link_manager_link_create(dap_stream_node_addr_t *a_node_addr)
{
// sanity check
    dap_return_val_if_pass_err(!s_link_manager, NULL, s_init_error);
    dap_return_val_if_pass(!a_node_addr || !a_node_addr->uint64, NULL);
    if (a_node_addr->uint64 == g_node_addr.uint64)
        return NULL;
// func work
    dap_link_t *l_ret = NULL;
    pthread_rwlock_wrlock(&s_link_manager->links_lock);
        HASH_FIND(hh, s_link_manager->links, a_node_addr, sizeof(*a_node_addr), l_ret);
        if (!l_ret) {
            l_ret = DAP_NEW_Z(dap_link_t);
            if (!l_ret) {
                log_it(L_CRITICAL,"%s", g_error_memory_alloc);
                pthread_rwlock_unlock(&s_link_manager->links_lock);
                return NULL;
            }
            dap_client_t *l_client = dap_client_new(s_client_delete_callback, s_client_error_callback, NULL);
            dap_client_set_is_always_reconnect(l_client, false);
            dap_client_set_active_channels_unsafe(l_client, "RCGEND");
            *l_ret = (dap_link_t) {
                .client = l_client,
                .link_manager = s_link_manager
            };
            l_ret->client->link_info.node_addr.uint64 = a_node_addr->uint64;
            l_ret->client->_inheritor = l_ret;
            HASH_ADD(hh, s_link_manager->links, client->link_info.node_addr, sizeof(l_ret->client->link_info.node_addr), l_ret);
            debug_if(s_debug_more, L_DEBUG, "Create new link to node " NODE_ADDR_FP_STR "", NODE_ADDR_FP_ARGS_S(l_ret->client->link_info.node_addr));
        } else {
            log_it(L_DEBUG, "Link "NODE_ADDR_FP_STR" already present", NODE_ADDR_FP_ARGS(a_node_addr));
        }
    pthread_rwlock_unlock(&s_link_manager->links_lock);
    return l_ret;
}

/**
 * @brief create or update link, if any arg NULL - it's not updated
 * @param a_link - updating link
 * @param a_host - host addr
 * @param a_port - host port
 * @param a_force - if false update only if link have state DISCONECTED
 * @return if ERROR null, other - pointer to dap_link_t
 */
dap_link_t *dap_link_manager_link_update(dap_link_t *a_link, const char *a_host, uint16_t a_port, bool a_force)
{
// sanity check
    dap_return_val_if_pass(!a_link || a_link->state == LINK_STATE_DELETED || !a_link->client, NULL);
// func work
    pthread_rwlock_rdlock(&s_link_manager->links_lock);
        if (a_link->state != LINK_STATE_DISCONNECTED && !a_force) {
            log_it(L_DEBUG, "Link "NODE_ADDR_FP_STR" not updated, please use force option", NODE_ADDR_FP_ARGS_S(a_link->client->link_info.node_addr));
            pthread_rwlock_unlock(&s_link_manager->links_lock);
            return a_link;
        }
        if (a_host)
            dap_strncpy(a_link->client->link_info.uplink_addr, a_host, DAP_HOSTADDR_STRLEN);
        if (a_port)
            a_link->client->link_info.uplink_port = a_port;
        a_link->valid = *a_link->client->link_info.uplink_addr && a_link->client->link_info.uplink_port && dap_strcmp(a_link->client->link_info.uplink_addr, "::");
        if (a_link->valid) {
            log_it(L_INFO, "Validate link to node " NODE_ADDR_FP_STR " with address %s : %d", NODE_ADDR_FP_ARGS_S(a_link->client->link_info.node_addr), a_link->client->link_info.uplink_addr, a_link->client->link_info.uplink_port);
        }
    pthread_rwlock_unlock(&s_link_manager->links_lock);
    return a_link;
}

/**
 * @brief add link to manager list
 * @param a_net_id - net id to link count
 * @param a_link - pointer to adding link
 * @return if ok 0, etoher if ERROR
 */
int dap_link_manager_link_add(uint64_t a_net_id, dap_link_t *a_link)
{
// sanity check
    dap_list_t *l_net_item = s_find_net_item_by_id(a_net_id);
    dap_return_val_if_pass(!l_net_item || !a_link || !s_link_manager->active || !s_check_active_nets(), -1);
// func work
    dap_link_t *l_link = NULL;
    pthread_rwlock_rdlock(&s_link_manager->links_lock);
        HASH_FIND(hh, s_link_manager->links, &a_link->client->link_info.node_addr, sizeof(a_link->client->link_info.node_addr), l_link);
    pthread_rwlock_unlock(&s_link_manager->links_lock);
    if (l_link != a_link) {
        log_it(L_WARNING, "LEAKS, links dublicate to node "NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS_S(a_link->client->link_info.node_addr));
        return -3;
    }
    s_client_connect(a_link, l_net_item->data);
    return 0;
}

/**
 * @brief add downlink to manager list
 * @param a_node_addr - pointer to node addr
 * @return if ok 0, other if ERROR
 */
int dap_link_manager_downlink_add(dap_stream_node_addr_t *a_node_addr)
{
// sanity check
    dap_link_t *l_link = dap_link_manager_link_create(a_node_addr);
    dap_return_val_if_pass(!l_link || !s_link_manager->active, -1);
// func work
    if (l_link->state != LINK_STATE_DISCONNECTED) {
        log_it(L_WARNING, "Get dowlink from "NODE_ADDR_FP_STR" with existed link", NODE_ADDR_FP_ARGS(a_node_addr));
        return -3;
    }
    dap_list_t *l_item = NULL;
    DL_FOREACH(l_link->static_links_clusters, l_item) {
        dap_cluster_member_add((dap_cluster_t *)l_item->data, &l_link->client->link_info.node_addr, 0, NULL);
    }
    l_link->state = LINK_STATE_DOWNLINK;
    log_it(L_INFO, "Get dowlink from "NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS(a_node_addr));
    return 0;
}

/**
 * @brief delete downlink from manager list
 * @param a_node_addr - pointer to node addr
 * @return if ok 0, other if ERROR
 */
void dap_link_manager_downlink_delete(dap_stream_node_addr_t *a_node_addr)
{
// sanity check
    dap_link_t *l_link = dap_link_manager_link_create(a_node_addr);
    dap_return_if_pass(!l_link || !s_link_manager->active);
// func work
    l_link->state = LINK_STATE_DISCONNECTED;
    debug_if(s_debug_more, L_DEBUG, "Deleting dowlink from "NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS(a_node_addr));
}

/**
 * @brief used to add member to link cluster if stream established
 * @param a_net_id - net id to check
 * @param a_node_addr - node addr to check
 */
void dap_accounting_downlink_in_net(uint64_t a_net_id, dap_stream_node_addr_t *a_node_addr)
{
// sanity check
    dap_managed_net_t *l_net = s_find_net_by_id(a_net_id);
    dap_return_if_pass(!l_net);
// func work
    if (l_net->active) {
        dap_cluster_member_add(l_net->node_link_cluster, a_node_addr, 0, NULL);
        s_debug_accounting_link_in_net(false, a_node_addr, l_net->id);
    }
}

/**
 * @brief set new condition to link manager
 * @param a_new_condition - true - manager active, false - inactive
 */
void dap_link_manager_set_condition(bool a_new_condition)
{
// sanity check
    dap_return_if_pass_err(!s_link_manager, s_init_error);
    dap_return_if_pass(a_new_condition == s_link_manager->active);
// func work
    s_link_manager->active = a_new_condition;
    if (s_link_manager->active) {
        s_link_manager->update_timer = dap_timerfd_start(s_timer_update_states, s_update_states, NULL);
        if (!s_link_manager->update_timer)
            log_it(L_WARNING, "Can't activate timer on link manager");
    }
}

/**
 * @brief get current link manager condition
 * @return current link manager condition
 */
DAP_INLINE bool dap_link_manager_get_condition()
{
// sanity check
    dap_return_val_if_pass_err(!s_link_manager, false, s_init_error);
// func work
    return s_link_manager->active;
}

/**
 * @brief add static links cluster to list
 * @param a_node_addr link manager condition
 * @param a_cluster links cluster to add
 */
void dap_link_manager_add_static_links_cluster(dap_stream_node_addr_t *a_node_addr, dap_cluster_t *a_cluster)
{
// sanity check
    dap_link_t *l_link = dap_link_manager_link_create(a_node_addr);
    dap_return_if_pass(!l_link || !a_cluster);
// func work
    l_link->static_links_clusters = dap_list_append(l_link->static_links_clusters, a_cluster);
    s_debug_cluster_adding_removing(true, true, a_cluster, &l_link->client->link_info.node_addr);
}

/**
 * @brief remove static links cluster from list
 * @param a_node_addr link manager condition
 * @param a_cluster links cluster to add
 */
void dap_link_manager_remove_static_links_cluster_all(dap_cluster_t *a_cluster)
{
// sanity check
    dap_return_if_pass_err(!s_link_manager, s_init_error);
    dap_return_if_pass(!a_cluster);
// func work
    dap_link_t *l_link = NULL, *l_tmp = NULL;
    pthread_rwlock_rdlock(&s_link_manager->links_lock);
        HASH_ITER(hh, s_link_manager->links, l_link, l_tmp) {
            l_link->static_links_clusters = dap_list_remove(l_link->static_links_clusters, a_cluster);
            s_debug_cluster_adding_removing(true, false, a_cluster, &l_link->client->link_info.node_addr);
        }
    pthread_rwlock_unlock(&s_link_manager->links_lock);
}

/**
 * @brief forming list with info about active links
 * @param a_net_id net id to search
 * @param a_count count of finded links
 * @return pointer to dap_link_info_t array
 */
dap_link_info_t *dap_link_manager_get_net_links_info_list(uint64_t a_net_id, size_t *a_count)
{
// sanity check
    dap_managed_net_t *l_net = s_find_net_by_id(a_net_id);
    dap_return_val_if_pass(!l_net, 0);
// func work
    size_t l_count = 0;
    dap_link_info_t *l_ret = NULL;
    dap_link_t *l_link = NULL, *l_tmp = NULL;
    dap_stream_node_addr_t *l_links_addrs = dap_cluster_get_all_members_addrs(l_net->node_link_cluster, &l_count);
    if (!l_links_addrs || !l_count) {
        return NULL;
    }
    DAP_NEW_Z_COUNT_RET_VAL(l_ret, dap_link_info_t, l_count, NULL, l_links_addrs);
    pthread_rwlock_rdlock(&s_link_manager->links_lock);
        for (int i = l_count - 1; i >= 0; --i) {
            dap_link_t *l_link = NULL;
            HASH_FIND(hh, s_link_manager->links, l_links_addrs + i, sizeof(l_links_addrs[i]), l_link);
            if (!l_link || l_link->state != LINK_STATE_ESTABLISHED) {
                --l_count;
                continue;
            }
            dap_mempcpy(l_ret + i, &l_link->client->link_info, sizeof(l_link->client->link_info));
        }
    pthread_rwlock_unlock(&s_link_manager->links_lock);
    DAP_DELETE(l_links_addrs);
    if (!l_count) {
        DAP_DELETE(l_ret);
        return NULL;
    }
    if (a_count)
        *a_count = l_count;
    return l_ret;
}

/**
 * @brief print information about links
 */
void s_link_manager_print_links_info()
{
    dap_link_t *l_link = NULL, *l_tmp = NULL;
    pthread_rwlock_rdlock(&s_link_manager->links_lock);
        printf(" State |\tNode addr\t|   Clusters\t|Static clusters|\tHost\t|\n"
                "-------------------------------------------------------------------------------\n");
        HASH_ITER(hh, s_link_manager->links, l_link, l_tmp) {
            printf("   %d   | "NODE_ADDR_FP_STR"\t|\t%"DAP_UINT64_FORMAT_U
                                                "\t|\t%"DAP_UINT64_FORMAT_U"\t| %s\n",
                                     l_link->state, NODE_ADDR_FP_ARGS_S(l_link->client->link_info.node_addr),
                                     dap_list_length(l_link->links_clusters),
                                     dap_list_length(l_link->static_links_clusters), l_link->client->link_info.uplink_addr);
        }
    pthread_rwlock_unlock(&s_link_manager->links_lock);
}
