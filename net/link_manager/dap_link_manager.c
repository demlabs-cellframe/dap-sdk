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
    uint64_t id;
    dap_list_t *link_clusters;
} dap_managed_net_t;

static bool s_debug_more = false;
static const char *s_init_error = "Link manager not inited";
static uint32_t s_timer_update_states = 4000;
static uint32_t s_min_links_num = 5;
static uint32_t s_max_attempts_num = 5;
static dap_link_manager_t *s_link_manager = NULL;

static void s_client_connect(dap_link_t *a_link, void *a_callback_arg);
static void s_client_connected_callback(dap_client_t *a_client, void *a_arg);
static void s_client_error_callback(dap_client_t *a_client, void *a_arg);
static void s_accounting_uplink_in_net(dap_link_t *a_link, dap_managed_net_t *a_net);
static void s_link_delete(dap_link_t *a_link, bool a_force);
static void s_link_delete_all(bool a_force);
static void s_links_wake_up(dap_link_manager_t *a_link_manager);
static void s_links_request(dap_link_manager_t *a_link_manager);
static void s_update_states(void *a_arg);
static void s_link_manager_print_links_info(dap_link_manager_t *a_link_manager);

static dap_list_t *s_find_net_item_by_id(uint64_t a_net_id)
{
    dap_return_val_if_pass_err(!s_link_manager, NULL, s_init_error);
    dap_return_val_if_pass(!a_net_id, NULL);
    dap_list_t *l_item = NULL;
    DL_FOREACH(s_link_manager->nets, l_item)
        if (a_net_id == ((dap_managed_net_t *)(l_item->data))->id)
            break;
    if (!l_item) {
        debuf_if(s_debug_more, L_ERROR, "Net ID %"DAP_UINT64_FORMAT_U" not controlled by link manager", a_net_id);
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

/**
 * @brief s_client_connected_callback
 * @param a_client - client to connect
 * @param a_arg - callback args, pointer dap_managed_net_t
 */
void s_client_connected_callback(dap_client_t *a_client, void UNUSED_ARG *a_arg)
{
// sanity check
    dap_return_if_pass(!a_client || !DAP_LINK(a_client) );
    dap_link_t *l_link = DAP_LINK(a_client);
// func work
    log_it(L_NOTICE, "Stream connection with node "NODE_ADDR_FP_STR" (%s:%hu) established",
                NODE_ADDR_FP_ARGS_S(l_link->client->link_info.node_addr),
                l_link->client->link_info.uplink_addr, l_link->client->link_info.uplink_port);
    l_link->uplink.attempts_count = 0;
    l_link->uplink.state = LINK_STATE_ESTABLISHED;
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
    bool l_is_last_attempt = a_arg;
    if (l_is_last_attempt) {
        l_link->state = LINK_STATE_DISCONNECTED;
// TODO
        if (!l_link->static_clusters)
            l_link->attempts_count++;
        if (l_link->link_manager->callbacks.disconnected)
            for (dap_list_t *it = l_link->associated_nets; it; it = it->next) {
                // if dynamic link call callback
                dap_managed_net_t *l_net = it->data;
                l_link->link_manager->callbacks.disconnected(l_link, l_net->id, dap_cluster_members_count(l_net->link_clusters->data);
            }
    }
    else if (l_link->link_manager->callbacks.error) // TODO make different error codes
        for (dap_list_t *it = l_link->associated_nets; it; it = it->next) {
            // if dynamic link call callback
            dap_managed_net_t *l_net = it->data;
            l_link->link_manager->callbacks.error(l_link, l_net->id, EINVAL);
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
    if (a_force) {
        dap_cluster_link_delete_from_all(&a_link->addr);
        dap_list_free(a_link->uplink.associated_nets);
    } else
        assert(a_link->uplink.associated_nets == NULL);
    assert(a_link->active_clusters == NULL);
    bool l_link_preserve = a_link->static_clusters && !a_force;
    if (a_link->uplink.client) {
        if (l_link_preserve) {
            if (a_link->uplink.state != LINK_STATE_DISCONNECTED)
                dap_client_go_stage(a_link->uplink.client, STAGE_BEGIN, NULL);
        } else
            dap_client_delete_mt(a_link->uplink.client);
    }
    if (!a_link->is_uplink) {
        // Drop downlink
        dap_worker_t *l_downlink_worker = NULL;
        dap_events_socket_uuid_t l_downlink_escoket_uuid = dap_stream_find_by_addr(a_link->addr, &l_downlink_worker);
        dap_events_socket_remove_and_delete_mt(l_downlink_worker, l_downlink_escoket_uuid);
    }
    if (l_link_preserve)
        return;
    dap_list_free(a_link->static_clusters);
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
void s_links_wake_up(dap_link_manager_t *a_link_manager)
{
    pthread_rwlock_rdlock(&a_link_manager->links_lock);
    for (dap_link_t *it = a_link_manager->links; it; it = it->hh.next) {
        if (it->active_clusters || !it->uplink.client)
            continue;
        if (a_link->uplink.state != LINK_STATE_DISCONNECTED)
            continue;
        if (!s_link_have_clusters_enabled())
            continue;
        if (dap_client_get_stage(a_link->client) != STAGE_BEGIN) {
            dap_client_go_stage(a_link->client, STAGE_BEGIN, NULL);
            log_it(L_ERROR, "Client " NODE_ADDR_FP_STR " state is not BEGIN, can't connect", NODE_ADDR_FP_ARGS_S(it->addr));
            continue;
        }
        if (!s_link_manager->callbacks.fill_net_info(l_link)) {
            log_it(L_WARNING,"Can't find node "NODE_ADDR_FP_STR" in node list", NODE_ADDR_FP_ARGS_S(it->addr));
            continue;
        }
        log_it(L_INFO, "Connecting to node " NODE_ADDR_FP_STR ", addr %s : %d", NODE_ADDR_FP_ARGS_S(a_link->client->link_info.node_addr), a_link->client->link_info.uplink_addr, a_link->client->link_info.uplink_port);
        a_link->state = LINK_STATE_CONNECTING ;
        dap_client_go_stage(a_link->client, STAGE_STREAM_STREAMING, s_client_connected_callback);
    }
    pthread_rwlock_unlock(&s_link_manager->links_lock);
}

/**
 * @brief Create request to new links
 */
void s_links_request(dap_link_manager_t *a_link_manager)
{
// func work
    // dynamic link work
    dap_list_t *l_item = NULL;
    DL_FOREACH(a_link_manager->nets, l_item) {
        dap_managed_net_t *l_net = (dap_managed_net_t *)l_item->data;
        if(l_net->active && a_link_manager->callbacks.link_request && l_net->links_count < a_link_manager->min_links_num)
            a_link_manager->callbacks.link_request(l_net->id);
    }
}

/**
 * @brief serially call funcs s_links_wake_up and s_links_request
 * @param a_arg UNUSED
 * @return false if error or manager inactiove, other - true
 */
void s_update_states(void *a_arg)
{
// sanity check
    dap_return_val_if_pass_err(!s_link_manager, false, s_init_error);
    assert(a_arg == s_link_manager);
    dap_link_manager_t *l_link_manager = a_arg;
    // if inactive return
    if (!l_link_manager->active)
        return;
    if (s_debug_more)
        s_link_manager_print_links_info();
    // static mode switcher
    static bool l_wakeup_mode = false;
    if (l_wakeup_mode)
        s_links_wake_up(l_link_manager);
    else
        s_links_request(l_link_manager);
    l_wakeup_mode = !l_wakeup_mode;
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
    if (dap_proc_thread_timer_add(NULL, s_update_states, s_link_manager, s_timer_update_states)) {
        log_it(L_ERROR, "Can't activate timer on link manager");
        return -2;
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
    return dap_cluster_members_count(l_net->node_link_cluster);
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
    size_t l_links_count = dap_link_manager_links_count(a_net_id);
    return l_links_count < s_link_manager->min_links_num ? s_link_manager->min_links_num - l_links_count : 0;
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
    dap_managed_net_t *l_net = s_find_net_by_id(a_net_id);
    if (!l_net) {
        DAP_NEW_Z_RET_VAL(l_net, dap_managed_net_t, -3, NULL);
        l_net->id = a_net_id;
        s_link_manager->nets = dap_list_append(s_link_manager->nets, (void *)l_net);
    }
    for (dap_list_t *it = l_net->link_clusters; it; it = it->next)
        if (it->data == a_link_cluster) {
            debug_if(s_debug_more, "Cluster GUUID %s already associated with net ID 0x%" DAP_UINT64_FORMAT_x,
                                                        dap_guuid_to_hex_str(a_link_cluster->guuid), l_net->id);
            return -4;
        }
    l_net->link_clusters = dap_list_append(l_net->link_clusters, a_link_cluster);
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
    dap_list_free(((dap_managed_net_t *)l_net_tem->data)->link_clusters);
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
    if (l_net->active == a_new_condition)
        return;
    l_net->active = a_new_condition;
    for (dap_list_t *it = l_net->link_clusters; it; it = it->next) {
        dap_cluster_t *l_cluster = it->data;
        if (a_new_condition)
            l_cluster->status = DAP_CLUSTER_STATUS_ENABLED;
        else {
            l_cluster->status = DAP_CLUSTER_STATUS_DISABLED;
            dap_cluster_delete_all_members(l_cluster);
        }
    }
    if (a_new_condition)
        return;
    pthread_rwlock_rdlock(&s_link_manager->links_lock);
    for (dap_link_t *l_link_it = s_link_manager->links; l_link_it; l_link_it = l_link_it->hh.next)
        for (dap_list_t *l_net_it = l_link_it->associated_nets; l_net_it; l_net_it = l_net_it->next)
            if (l_net_it->data == l_net) {
                l_link_it->associated_nets = dap_list_remove_link(l_link_it->associated_nets, l_net_it);
                break;
            }
    pthread_rwlock_unlock(&s_link_manager->links_lock);
}

/**
 * @brief check adding member in links cluster
 * @param a_addr - node addr to adding
 * @param a_cluster - pointer to links cluster
 */
void dap_link_manager_add_links_cluster(dap_cluster_member_t *a_member, void *a_arg)
{
// sanity check
    dap_return_if_pass(!s_link_manager || !a_member || !a_member->cluster);
    assert(a_arg = a_member->cluster);
// func work
    dap_link_t *l_link = dap_link_manager_link_find(&a_member->addr);
    if (!l_link) {
        log_it(L_ERROR, "Try cluster adding to non-existent link");
        return;
    }
    l_link->links_clusters = dap_list_append(l_link->active_clusters, a_member->cluster);
    s_debug_cluster_adding_removing(false, true, a_member->cluster, &a_member->addr);
}

/**
 * @brief check removing member from links cluster
 * @param a_addr - node addr to adding
 * @param a_cluster - pointer to links cluster
 */
void dap_link_manager_remove_links_cluster(dap_cluster_member_t *a_member, void *a_arg)
{
    dap_return_if_pass(!s_link_manager || !a_member || !a_member->cluster);
    assert(a_arg = a_member->cluster);
    dap_link_t *l_link = dap_link_manager_link_find(&a_member->addr);
    if (!l_link) {
        log_it(L_ERROR, "Try cluster deleting from non-existent link");
        return;
    }
    l_link->links_clusters = dap_list_remove(l_link->active_clusters, a_member->cluster);
    s_debug_cluster_adding_removing(false, false, a_member->cluster, &a_member->addr);
    if (!l_link->active_clusters) {
        pthread_rwlock_wrlock(&s_link_manager->links_lock);
        s_link_delete(l_link, false);
        pthread_rwlock_unlock(&s_link_manager->links_lock);
    }
}


/**
 * @brief create a link, if a_addr is NULL - it's not updated
 * @param a_node_addr - node addr to adding
 * @return if ERROR null, other - pointer to dap_link_t
 */
dap_link_t *dap_link_manager_link_create(dap_stream_node_addr_t *a_node_addr, uint64_t a_associated_net_id)
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
            goto unlock;
        }
        HASH_ADD(hh, s_link_manager->links, addr, sizeof(*a_node_addr), l_ret);
        debug_if(s_debug_more, L_DEBUG, "Create new link to node " NODE_ADDR_FP_STR "", NODE_ADDR_FP_ARGS_S(l_ret->client->link_info.node_addr));
        l_ret->addr.uin64 = a_node_addr->uint64;
        l_ret->link_manager = s_link_manager;
    }
    if (a_associated_net_id != DAP_NET_ID_INVALID) {
        dap_managed_net_t *l_net = s_find_net_by_id(a_associated_net_id);
        if (!l_net)
            goto unlock;
        for (dap_list_t *it = l_ret->associated_nets; it; it = it->next)
            if (((dap_managed_net_t *)it->data)->id == a_associated_net_id) {
                debug_if(s_debug_more, "Net ID 0x%" DAP_UINT64_FORMAT_x " already associated with link " NODE_ADDR_FP_STR,
                                                            a_associated_net_id, NODE_ADDR_FPS_ARGS(a_node_addr));
                goto unlock;
            }
        l_ret->associated_nets = dap_list_append(l_ret->associated_nets, l_net);
        if (l_ret->uplink.client && l_ret->uplink.state == LINK_STATE_ESTABLISHED) {
            dap_cluster_member_add(a_net->node_link_cluster, &a_link->client->link_info.node_addr, 0, NULL);
            if (a_link->link_manager->callbacks.connected)
                a_link->link_manager->callbacks.connected(a_link, a_net->id);
            s_debug_accounting_link_in_net(true, &a_link->client->link_info.node_addr, a_net->id);
        }
    }
unlock:
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
int dap_link_manager_link_update(dap_link_t *a_link, const char *a_host, uint16_t a_port, bool a_force)
{
// sanity check
    dap_return_val_if_pass(!a_link || a_link->state == LINK_STATE_DELETED || !a_link->client, -1);
    if (!a_host || !a_port || !strcmp(l_host_addr, "::")) {
        log_it(L_ERROR, "Incomplete link info for uplink update");
        return -2;
    }
// func work
    pthread_rwlock_rdlock(&s_link_manager->links_lock);
        if (a_link->uplink.client && a_link->state != LINK_STATE_DISCONNECTED && !a_force) {
            log_it(L_DEBUG, "Link " NODE_ADDR_FP_STR " not updated, please use force option", NODE_ADDR_FP_ARGS_S(a_link->client->link_info.node_addr));
            pthread_rwlock_unlock(&s_link_manager->links_lock);
            return -3;
        }
        if (!a_link->uplink.client)
            a_link->uplink.client = dap_client_new(s_client_delete_callback, s_client_error_callback, NULL);
        dap_client_t *l_client = a_link->uplink.client;
        dap_client_set_uplink_unsafe(l_client, a_node_addr, a_host, a_port);
        dap_client_set_is_always_reconnect(l_client, false);
        dap_client_set_active_channels_unsafe(l_client, "RCGEND");
        l_client->_inheritor = a_link;
        log_it(L_INFO, "Validate link to node " NODE_ADDR_FP_STR " with address %s : %d", NODE_ADDR_FP_ARGS_S(a_link->client->link_info.node_addr),
                                                    a_link->client->link_info.uplink_addr, a_link->client->link_info.uplink_port);
    pthread_rwlock_unlock(&s_link_manager->links_lock);
    return 0;
}

/**
 * @brief find a link
 * @param a_node_addr - node addr to adding
 * @return if ERROR null, other - pointer to dap_link_t
 */
dap_link_t *dap_link_manager_link_find(dap_stream_node_addr_t *a_node_addr)
{
// sanity check
    dap_return_val_if_pass_err(!s_link_manager, NULL, s_init_error);
    dap_return_val_if_pass(!a_node_addr || !a_node_addr->uint64, NULL);
    dap_link_t *l_ret = NULL;
    pthread_rwlock_rdlock(&s_link_manager->links_lock);
    HASH_FIND(hh, s_link_manager->links, a_node_addr, sizeof(*a_node_addr), l_ret);
    pthread_rwlock_unlock(&s_link_manager->links_lock);
    return l_ret;
}

/**
 * @brief add downlink to manager list
 * @param a_node_addr - pointer to node addr
 * @return if ok 0, other if ERROR
 */
int dap_link_manager_stream_add(dap_stream_node_addr_t *a_node_addr, bool a_uplink)
{
// sanity check
    dap_return_val_if_pass(!l_link || !s_link_manager->active, -1);
// func work
    dap_link_t *l_link = dap_link_manager_link_find(a_node_addr);
    if (!l_link)
        l_link = dap_link_manager_link_create(a_node_addr, DAP_NET_ID_INVALID);
    if (!l_link) {
        log_it(L_ERROR, "Can't create link for address " NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS(a_node_addr));
        return;
    }
    if (l_link->active_clusters) {
        log_it(L_ERROR, "%s " NODE_ADDR_FP_STR " with existed link",
                        a_uplink ? "Set uplink to" : "Get dowlink from", NODE_ADDR_FP_ARGS(a_node_addr));
        return -3;
    }
    dap_list_t *l_item = NULL;
    DL_FOREACH(l_link->static_clusters, l_item) {
        dap_cluster_t *l_cluster = l_item->data;
        if (l_cluster->status == DAP_CLUSTER_STATUS_ENABLED)
            dap_cluster_member_add(l_info->cluster, a_node_addr, 0, NULL);
    }
    if (a_iplink) {
        for (dap_list_t *it = l_link->associated_nets; it; it = it->next) {
            // if dynamic link, add net cluster to list and call callback
            dap_managed_net_t *l_net = it->data;
            if (l_net && l_net->active) {
                dap_cluster_member_add(l_net->node_link_cluster, &a_client->link_info.node_addr, 0, NULL);
                if (l_link->link_manager->callbacks.connected)
                    l_link->link_manager->callbacks.connected(l_link, l_net->id);
            }
        }
    }
    l_link->is_uplink = a_uplink;
    log_it(L_INFO, "%s " NODE_ADDR_FP_STR, a_uplink ? "Set uplink to" : "Get dowlink from", NODE_ADDR_FP_ARGS(a_node_addr));
    return 0;
}

void dap_link_manager_stream_replace(bool a_old_is_uplink, bool a_new_is_uplink)
{
//TODO
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
    if (!l_net->active)
        return;
    s_debug_accounting_link_in_net(false, a_node_addr, l_net->id);
    dap_link_t *l_link = dap_link_manager_link_find(a_node_addr);
    assert(l_link);
    dap_cluster_member_add(l_net->node_link_cluster, a_node_addr, 0, NULL);
    s_debug_cluster_adding_removing(false, false, l_net->node_link_cluster, a_node_addr);
}

/**
 * @brief delete downlink from manager list
 * @param a_node_addr - pointer to node addr
 * @return if ok 0, other if ERROR
 */
void dap_link_manager_downlink_delete(dap_stream_node_addr_t *a_node_addr)
{
// sanity check
    dap_link_t *l_link = dap_link_manager_link_find(a_node_addr);
    dap_return_if_pass(!l_link || !s_link_manager->active);
// func work
    debug_if(s_debug_more, L_DEBUG, "Deleting dowlink from "NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS(a_node_addr));
    pthread_rwlock_wrlock(&s_link_manager->links_lock);
    s_link_delete(a_link, false);
    pthread_rwlock_unlock(&s_link_manager->links_lock);
}

/**
 * @brief set new condition to link manager
 * @param a_new_condition - true - manager active, false - inactive
 */
DAP_INLINE void dap_link_manager_set_condition(bool a_new_condition)
{
// sanity check
    dap_return_if_pass_err(!s_link_manager, s_init_error);
    s_link_manager->active = a_new_condition;
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
void dap_link_manager_add_static_links_cluster(dap_cluster_member_t *a_member, void *a_arg)
{
// sanity check
    dap_return_if_pass_err(!s_link_manager, s_init_error);
    dap_return_if_pass(!a_member || !a_arg);
// func work
    dap_stream_node_addr_t *l_node_addr = &a_member->addr;
    dap_cluster_t *l_cluster = a_arg;
    dap_link_t *l_link = dap_link_manager_link_find(l_node_addr);
    if (!l_link)
        l_link = dap_link_manager_link_create(l_node_addr, DAP_NET_ID_INVALID);
    l_link->static_clusters = dap_list_append(l_link->static_clusters, l_cluster);
    s_debug_cluster_adding_removing(true, true, l_cluster, l_node_addr);
}

/**
 * @brief remove static links cluster from list
 * @param a_node_addr link manager condition
 * @param a_cluster links cluster to add
 */
void dap_link_manager_remove_static_links_cluster(dap_cluster_member_t *a_member, void *a_arg)
{
// sanity check
    dap_return_if_pass_err(!s_link_manager, s_init_error);
    dap_return_if_pass(!a_member || !a_arg);
// func work
    dap_stream_node_addr_t *l_node_addr = &a_member->addr;
    dap_cluster_t *l_cluster = a_arg;
    dap_link_t *l_link = dap_link_manager_link_find(l_node_addr);
    if (!l_link) {
        debug_if(s_debug_more, L_ERROR, "Link " NODE_ADDR_FP_STR " not found", NODE_ADDR_FP_ARGS(l_node_addr));
        return;
    }
    l_link->static_clusters = dap_list_remove(l_link->static_clusters, l_cluster);
    s_debug_cluster_adding_removing(true, false, l_cluster, l_node_addr);
    if (!l_link->static_clusters && !l_link->active_clusters) {
        pthread_rwlock_wrlock(&s_link_manager->links_lock);
        s_link_delete(l_link, false);
        pthread_rwlock_unlock(&s_link_manager->links_lock);
    }
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
void s_link_manager_print_links_info(dap_link_manager_t *a_link_manager)
{
    dap_link_t *l_link = NULL, *l_tmp = NULL;
    pthread_rwlock_rdlock(&a_link_manager->links_lock);
        printf(" State |\tNode addr\t|   Clusters\t|Static clusters|\tHost\t|\n"
                "-------------------------------------------------------------------------------\n");
        HASH_ITER(hh, a_link_manager->links, l_link, l_tmp) {
            printf("   %d   | "NODE_ADDR_FP_STR"\t|\t%"DAP_UINT64_FORMAT_U
                                                "\t|\t%"DAP_UINT64_FORMAT_U"\t| %s\n",
                                     l_link->state, NODE_ADDR_FP_ARGS_S(l_link->client->link_info.node_addr),
                                     dap_list_length(l_link->links_clusters),
                                     dap_list_length(l_link->static_clusters), l_link->client->link_info.uplink_addr);
        }
    pthread_rwlock_unlock(&a_link_manager->links_lock);
}