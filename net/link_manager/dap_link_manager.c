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
#include "dap_global_db_driver.h"
#include "dap_stream_cluster.h"
#include "dap_worker.h"
#include "dap_config.h"
#include "dap_strfuncs.h"
#include "dap_client_pvt.h"
#include "dap_net.h"
#include <semaphore.h>
#include <time.h>

#define LOG_TAG "dap_link_manager"

#define DAP_LINK(a) ((dap_link_t *)(a)->_inheritor)

static const char s_heated_group_local_prefix[] = "local.nodes.heated.0x";
static const uint64_t s_cooling_period = 900 /*sec*/ * 1000000000LLU;

typedef struct dap_managed_net {
    bool active;
    uint64_t id;
    uint32_t uplinks;
    uint32_t min_links_num;     // min links required in each net
    dap_list_t *link_clusters;
} dap_managed_net_t;

static bool s_debug_more = false;
static const char *s_init_error = "Link manager not inited";
static uint32_t s_timer_update_states = 5000;
static uint32_t s_max_attempts_num = 1;
static uint32_t s_reconnect_delay = 20; // sec
static dap_link_manager_t *s_link_manager = NULL;
static dap_proc_thread_t *s_query_thread = NULL;

static void s_client_connect(dap_link_t *a_link, void *a_callback_arg);
static void s_client_connected_callback(dap_client_t *a_client, void *a_arg);
static void s_client_error_callback(dap_client_t *a_client, void *a_arg);
static void s_accounting_uplink_in_net(dap_link_t *a_link, dap_managed_net_t *a_net);
static void s_link_delete(dap_link_t **a_link, bool a_force, bool a_client_preserve);
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
        debug_if(s_debug_more, L_ERROR, "Net ID 0x%016" DAP_UINT64_FORMAT_x " not controlled by link manager", a_net_id);
        return NULL;
    }
    return l_item;
}

DAP_STATIC_INLINE dap_managed_net_t *s_find_net_by_id(uint64_t a_net_id)
{
    dap_list_t *l_item = s_find_net_item_by_id(a_net_id);
    if (l_item)
        return (dap_managed_net_t *)l_item->data;
    log_it(L_ERROR, "Net ID 0x%016" DAP_UINT64_FORMAT_x " is not registered", a_net_id);
    return NULL;
}

/**
 * @brief forming group name for each net
 * @return NULL if error other group name
 */
DAP_STATIC_INLINE char *s_hot_group_forming(uint64_t a_net_id)
{ 
    return dap_strdup_printf("%s%016"DAP_UINT64_FORMAT_x, s_heated_group_local_prefix, a_net_id);
}

/**
 * @brief update hot list
 * @return NOT 0 if list empty
 */
static int s_update_hot_list(uint64_t a_net_id)
{
    size_t l_node_count = 0;
    char *l_hot_group = s_hot_group_forming(a_net_id);
    dap_global_db_obj_t *l_objs = dap_global_db_get_all_sync(l_hot_group, &l_node_count);
    if (!l_node_count || !l_objs) {
        log_it(L_DEBUG, "Hot list is empty");
        DAP_DEL_Z(l_hot_group);
        return 1;
    }
    dap_nanotime_t l_time_now = dap_nanotime_now();
    size_t l_deleted = 0;
    for(size_t i = 0; i < l_node_count; ++i) {
        if(l_time_now > l_objs[i].timestamp + s_cooling_period) {
            dap_global_db_del_sync(l_hot_group, l_objs[i].key);
            ++l_deleted;
        }
    }
    DAP_DEL_Z(l_hot_group);
    dap_global_db_objs_delete(l_objs, l_node_count);
    if (l_deleted == l_node_count) {
        log_it(L_DEBUG, "Hot list cleared");
        return 2;
    }
    return 0;
}

/**
 * @brief add node add in local GDB group
 * @param a_node_addr - node addr to adding
 */
static void s_node_hot_list_add(dap_stream_node_addr_t a_node_addr, uint64_t a_associated_net_id)
{
// sanity check
    dap_return_if_pass(!a_node_addr.uint64);
// func work
    const char *l_node_addr_str = dap_stream_node_addr_to_str_static(a_node_addr);
    char *l_hot_group = s_hot_group_forming(a_associated_net_id);
    dap_global_db_set_sync(l_hot_group, l_node_addr_str, NULL, 0, false);
    DAP_DEL_Z(l_hot_group);
}

// debug_more funcs
DAP_STATIC_INLINE void s_debug_cluster_adding_removing(bool a_static, bool a_adding, dap_cluster_t *a_cluster, dap_stream_node_addr_t *a_node_addr)
{
    debug_if(s_debug_more, L_DEBUG, "%s cluster net_id 0x%016" DAP_UINT64_FORMAT_x ", srv_id 0x%016" DAP_UINT64_FORMAT_x
                                    " successfully %s link " NODE_ADDR_FP_STR,
            a_static ? "Static" : "Links", 
            a_cluster->guuid.net_id,
            a_cluster->guuid.srv_id,
            a_adding ? "added to" : "removed from",
            NODE_ADDR_FP_ARGS(a_node_addr));
}

DAP_STATIC_INLINE void s_debug_accounting_link_in_net(bool a_uplink, dap_stream_node_addr_t *a_node_addr, uint64_t a_net_id)
{
    debug_if(s_debug_more, L_DEBUG, "Accounting %s " NODE_ADDR_FP_STR " in net %" DAP_UINT64_FORMAT_U,
                        a_uplink ? "uplink to" : "downlink from", NODE_ADDR_FP_ARGS(a_node_addr), a_net_id);
}

DAP_STATIC_INLINE void s_link_manager_print_links_info(dap_link_manager_t *a_link_manager)
{
    dap_link_t *l_link = NULL, *l_tmp = NULL;
    dap_string_t *l_report = dap_string_new("\n| Uplink |\tNode addr\t|Active Clusters|Static clusters|\tNet IDs\t\n"
            "-----------------------------------------------------------------\n");
    HASH_ITER(hh, a_link_manager->links, l_link, l_tmp) {
        dap_string_append_printf(l_report, "| %5s  |"NODE_ADDR_FP_STR"|\t%"DAP_UINT64_FORMAT_U
                                            "\t|\t%"DAP_UINT64_FORMAT_U"\t| ",
                                 l_link->is_uplink ? "True" : "False",
                                 NODE_ADDR_FP_ARGS_S(l_link->addr),
                                 dap_list_length(l_link->active_clusters),
                                 dap_list_length(l_link->static_clusters));
        dap_list_t *it, *tmp;
        DL_FOREACH_SAFE(l_link->uplink.associated_nets, it, tmp) {
            dap_managed_net_t *l_net = it->data;
            dap_string_append_printf(l_report, " %"DAP_UINT64_FORMAT_x, l_net->id);
        }
        dap_string_append_printf(l_report, "%s", "\n");
    }
    log_it(L_DEBUG, "%s", l_report->str);
    dap_string_free(l_report, true);
}

// General functional

/**
 * @brief link manager initialisation
 * @param a_callbacks - callbacks
 * @return 0 if ok, other if error
 */
int dap_link_manager_init(const dap_link_manager_callbacks_t *a_callbacks)
{
// sanity check
    dap_return_val_if_pass_err(s_link_manager, -2, "Link manager actualy inited");
// get config
    s_timer_update_states = dap_config_get_item_uint32_default(g_config, "link_manager", "timer_update_states", s_timer_update_states);
    s_max_attempts_num = dap_config_get_item_uint32_default(g_config, "link_manager", "max_attempts_num", s_max_attempts_num);
    s_reconnect_delay = dap_config_get_item_uint32_default(g_config, "link_manager", "reconnect_delay", s_reconnect_delay);
    s_debug_more = dap_config_get_item_bool_default(g_config,"link_manager","debug_more", s_debug_more);
    if (!(s_query_thread = dap_proc_thread_get_auto())) {
        log_it(L_ERROR, "Can't choose query thread on link manager");
        return -1;
    }
    if (!(s_link_manager = dap_link_manager_new(a_callbacks))) {
        log_it(L_ERROR, "Default link manager not inited");
        return -2;
    }
    if (dap_proc_thread_timer_add(s_query_thread, s_update_states, s_link_manager, s_timer_update_states)) {
        log_it(L_ERROR, "Can't activate timer on link manager");
        return -3;
    }
// clean ignore and connections group
    dap_list_t *l_groups = dap_global_db_driver_get_groups_by_mask(s_heated_group_local_prefix);
    dap_list_t *l_item_cut = NULL;
    DL_FOREACH(l_groups, l_item_cut) {
        dap_global_db_erase_table_sync((const char*)(l_item_cut->data));
    }
    dap_list_free_full(l_groups, NULL);
// start
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
    dap_link_t *l_link = NULL, *l_link_tmp;
    // No lock needed during shutdown - all operations stopped
    HASH_ITER(hh, s_link_manager->links, l_link, l_link_tmp)
        s_link_delete(&l_link, true, false);
    dap_list_t *it = NULL, *tmp;
    DL_FOREACH_SAFE(s_link_manager->nets, it, tmp)
        dap_link_manager_remove_net(((dap_managed_net_t *)it->data)->id);
    pthread_rwlock_destroy(&s_link_manager->nets_lock);
    DAP_DELETE(s_link_manager);
}

/**
 * @brief close connections and memory free
 * @param a_callbacks - callbacks
 * @return pointer to dap_link_manager_t, NULL if error
 */
dap_link_manager_t *dap_link_manager_new(const dap_link_manager_callbacks_t *a_callbacks)
{
    dap_return_val_if_pass_err(!a_callbacks || !a_callbacks->fill_net_info, NULL, "Needed link manager callbacks not filled, please check it");
    dap_link_manager_t *l_ret = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_link_manager_t, NULL);
    l_ret->callbacks = *a_callbacks;
    if(!l_ret->callbacks.link_request)
        log_it(L_WARNING, "Link manager link_request callback is NULL");
    l_ret->max_attempts_num = s_max_attempts_num;
    l_ret->reconnect_delay = s_reconnect_delay;
    pthread_rwlock_init(&l_ret->nets_lock, NULL);
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
    return dap_cluster_members_count((dap_cluster_t *)l_net->link_clusters->data);
}

/**
 * @brief return required links in concretic net
 * @param a_net_id net id for search
 * @return required links
 */
size_t dap_link_manager_required_links_count(uint64_t a_net_id)
{
// sanity check
    dap_managed_net_t *l_net = s_find_net_by_id(a_net_id);
    dap_return_val_if_pass(!s_link_manager || !l_net, 0);
// func work
    return l_net->min_links_num;
}

/**
 * @brief count needed links in concretic net
 * @param a_net_id net id for search
 * @return needed links count
 */
size_t dap_link_manager_needed_links_count(uint64_t a_net_id)
{
// sanity check
    dap_managed_net_t *l_net = s_find_net_by_id(a_net_id);
    dap_return_val_if_pass(!s_link_manager, 0);
// func work
    if (!l_net) {
        log_it(L_ERROR, "Net ID 0x%016" DAP_UINT64_FORMAT_x " is not registered", a_net_id);
        return 0;
    }
    return l_net->uplinks < l_net->min_links_num ? l_net->min_links_num - l_net->uplinks : 0;
}

/**
 * @brief add controlled net to link manager
 * @param a_net_id net id for adding
 * @param a_link_cluster net link cluster for adding
 * @return 0 if ok, other - ERROR
 */
int dap_link_manager_add_net(uint64_t a_net_id, dap_cluster_t *a_link_cluster, uint32_t a_min_links_number)
{
    dap_return_val_if_pass(!s_link_manager || !a_net_id || !a_link_cluster, -2);
    dap_list_t *l_item = NULL;
    pthread_rwlock_wrlock(&s_link_manager->nets_lock);
    DL_FOREACH(s_link_manager->nets, l_item) {
        if (a_net_id == ((dap_managed_net_t *)(l_item->data))->id) {
            log_it(L_ERROR, "Net ID 0x%016" DAP_UINT64_FORMAT_x " already managed", a_net_id);
            return -3;
        }
    }
    dap_managed_net_t *l_net = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_managed_net_t, -3);
    l_net->id = a_net_id;
    l_net->min_links_num = a_min_links_number;
    l_net->link_clusters = dap_list_append(l_net->link_clusters, a_link_cluster);
    s_link_manager->nets = dap_list_append(s_link_manager->nets, (void *)l_net);
    pthread_rwlock_unlock(&s_link_manager->nets_lock);
    return 0;
}

int dap_link_manager_add_net_associate(uint64_t a_net_id, dap_cluster_t *a_link_cluster)
{
    dap_return_val_if_pass(!s_link_manager || !a_net_id, -2);
    dap_managed_net_t *l_net = s_find_net_by_id(a_net_id);
    if (!l_net) {
        log_it(L_ERROR, "Net ID 0x%016" DAP_UINT64_FORMAT_x " not managed yet. Add net first", a_net_id);
        return -3;
    }
    for (dap_list_t *it = l_net->link_clusters; it; it = it->next)
        if (it->data == a_link_cluster) {
            debug_if(s_debug_more, L_ERROR, "Cluster GUUID %s already associated with net ID 0x%" DAP_UINT64_FORMAT_x,
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
    dap_managed_net_t *l_net = l_net_item->data;
    dap_link_manager_set_net_condition(l_net->id, false);
    s_link_manager->nets = dap_list_remove_link(s_link_manager->nets, l_net_item);
    dap_list_free(l_net->link_clusters);
    DAP_DEL_MULTY(l_net, l_net_item);
}

static bool s_net_condition_cleanup_callback(void *a_arg)
{
    dap_managed_net_t *l_net = (dap_managed_net_t*)a_arg;
    
    // No lock needed - this runs in s_query_thread sequentially
    dap_link_t *l_link_it, *l_link_tmp;
    HASH_ITER(hh, s_link_manager->links, l_link_it, l_link_tmp) {
        dap_list_t *l_net_it, *l_net_tmp;
        DL_FOREACH_SAFE(l_link_it->uplink.associated_nets, l_net_it, l_net_tmp) {
            if (l_net_it->data == l_net) {
                l_link_it->uplink.associated_nets = dap_list_delete_link(l_link_it->uplink.associated_nets, l_net_it);
                if (!l_link_it->uplink.associated_nets)
                    s_link_delete(&l_link_it, false, false);
                break;
            }
        }
    }
    return false;
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
    
    // Schedule async cleanup of links for this net
    dap_proc_thread_callback_add_pri(s_query_thread, s_net_condition_cleanup_callback, l_net, DAP_QUEUE_MSG_PRIORITY_HIGH);
}

bool dap_link_manager_get_net_condition(uint64_t a_net_id)
{
    dap_managed_net_t *l_net = s_find_net_by_id(a_net_id);
    dap_return_val_if_pass(!l_net, false);
    return l_net->active;
}

static dap_link_t *s_link_manager_link_find(dap_stream_node_addr_t *a_node_addr)
{
// sanity check
    dap_return_val_if_pass_err(!s_link_manager, NULL, s_init_error);
    dap_return_val_if_pass(!a_node_addr || !a_node_addr->uint64, NULL);
    dap_link_t *ret = NULL;
    HASH_FIND(hh, s_link_manager->links, a_node_addr, sizeof(*a_node_addr), ret);
    return ret;
}

/**
 * @brief check adding member in links cluster
 * @param a_addr - node addr to adding
 * @param a_cluster - pointer to links cluster
 */
struct cluster_operation_args {
    dap_stream_node_addr_t addr;
    dap_cluster_t *cluster;
    bool adding;  // true = add, false = remove
};

static bool s_cluster_operation_callback(void *a_arg)
{
    struct cluster_operation_args *l_args = a_arg;
    // No lock needed - this runs in s_query_thread sequentially
    dap_link_t *l_link = s_link_manager_link_find(&l_args->addr);
    if ( l_link ) {
        l_link->active_clusters = l_args->adding
            ? dap_list_append(l_link->active_clusters, l_args->cluster)
            : dap_list_remove(l_link->active_clusters, l_args->cluster);
        s_debug_cluster_adding_removing(false, l_args->adding, l_args->cluster, &l_args->addr);
    } else
        log_it(L_ERROR, "Cluster operation on non-existent link %s", dap_stream_node_addr_to_str_static(l_args->addr));
    DAP_DELETE(l_args);
    return false;
}

void dap_link_manager_add_links_cluster(dap_cluster_member_t *a_member, void UNUSED_ARG *a_arg)
{
// sanity check
    dap_return_if_pass(!s_link_manager || !a_member || !a_member->cluster);
// func work
    struct cluster_operation_args *l_args = DAP_NEW_Z_RET_IF_FAIL(struct cluster_operation_args);
    l_args->addr = a_member->addr;
    l_args->cluster = a_member->cluster;
    l_args->adding = true;
    
    dap_proc_thread_callback_add_pri(s_query_thread, s_cluster_operation_callback, l_args, DAP_QUEUE_MSG_PRIORITY_HIGH);
}

/**
 * @brief check removing member from links cluster
 * @param a_addr - node addr to adding
 * @param a_cluster - pointer to links cluster
 */
void dap_link_manager_remove_links_cluster(dap_cluster_member_t *a_member, void UNUSED_ARG *a_arg)
{
    dap_return_if_pass(!s_link_manager || !a_member || !a_member->cluster);
    
    struct cluster_operation_args *l_args = DAP_NEW_Z_RET_IF_FAIL(struct cluster_operation_args);
    l_args->addr = a_member->addr;
    l_args->cluster = a_member->cluster;
    l_args->adding = false;
    
    dap_proc_thread_callback_add_pri(s_query_thread, s_cluster_operation_callback, l_args, DAP_QUEUE_MSG_PRIORITY_HIGH);
}


struct client_connected_args {
    dap_client_t *client;
    dap_stream_node_addr_t addr;
};

static bool s_client_connected_callback_async(void *a_arg)
{
    struct client_connected_args *l_args = a_arg;
    dap_return_val_if_pass(!l_args->client || !DAP_LINK(l_args->client), false);
    
    // No lock needed - this runs in s_query_thread sequentially
    dap_link_t *l_link = s_link_manager_link_find(&l_args->addr);
    if (l_link && l_link == DAP_LINK(l_args->client)) {
        log_it(L_NOTICE, "Stream connection with node "NODE_ADDR_FP_STR" (%s:%hu) established",
                NODE_ADDR_FP_ARGS_S(l_link->uplink.client->link_info.node_addr),
                l_link->uplink.client->link_info.uplink_addr, l_link->uplink.client->link_info.uplink_port);
        l_link->uplink.attempts_count = 0;
        l_link->uplink.state = LINK_STATE_ESTABLISHED;
        l_link->uplink.es_uuid = DAP_CLIENT_PVT(l_args->client)->stream_es->uuid;
    } else
        log_it(L_ERROR, "Link with "NODE_ADDR_FP_STR" already dropped!", NODE_ADDR_FP_ARGS(&l_args->addr));
    
    DAP_DELETE(l_args);
    return false;
}

/**
 * @brief s_client_connected_callback
 * @param a_client - client to connect
 * @param a_arg - callback args, pointer dap_managed_net_t
 */
void s_client_connected_callback(dap_client_t *a_client, void *a_arg)
{
    dap_return_if_pass(!a_client || !DAP_LINK(a_client));
    dap_stream_node_addr_t *l_addr = (dap_stream_node_addr_t*)a_arg;
    
    struct client_connected_args *l_args = DAP_NEW_Z_RET_IF_FAIL(struct client_connected_args);
    l_args->client = a_client;
    l_args->addr = *l_addr;
    
    dap_proc_thread_callback_add_pri(s_query_thread, s_client_connected_callback_async, l_args, DAP_QUEUE_MSG_PRIORITY_HIGH);
}

void s_link_drop(dap_link_t *a_link, bool a_disconnected)
{
    if (a_disconnected) {
        a_link->uplink.state = LINK_STATE_DISCONNECTED;
        a_link->uplink.start_after = dap_time_now() + a_link->link_manager->reconnect_delay;
        if (++a_link->uplink.attempts_count < a_link->link_manager->max_attempts_num) {
            dap_client_go_stage(a_link->uplink.client, STAGE_BEGIN, NULL);
            return;
        }
        if (a_link->link_manager->callbacks.disconnected) {
            dap_list_t *it, *tmp;
            DL_FOREACH_SAFE(a_link->uplink.associated_nets, it, tmp) {
                dap_managed_net_t *l_net = it->data;
                if (!l_net->active) {
                    debug_if(s_debug_more, L_ERROR, "Link " NODE_ADDR_FP_STR " have associated net ID 0x%016" DAP_UINT64_FORMAT_x " have inactive state",
                                                                NODE_ADDR_FP_ARGS_S(a_link->addr), l_net->id);
                    DL_DELETE(a_link->uplink.associated_nets, it);
                    continue;
                }
                bool l_is_permanent_link = a_link->link_manager->callbacks.disconnected(
                            a_link, l_net->id, dap_cluster_members_count((dap_cluster_t *)l_net->link_clusters->data));
                if (l_is_permanent_link)
                    continue;
                DL_DELETE(a_link->uplink.associated_nets, it);
            }
        }
        if (!a_link->active_clusters && !a_link->uplink.associated_nets && !a_link->static_clusters) {
            s_link_delete(&a_link, false, false);
        } else 
            dap_client_go_stage(a_link->uplink.client, STAGE_BEGIN, NULL);
        if (a_link)
            a_link->uplink.attempts_count = 0;
    } else if (a_link->link_manager->callbacks.error) {// TODO make different error codes
        for (dap_list_t *it = a_link->uplink.associated_nets; it; it = it->next) {
            // if dynamic link call callback
            dap_managed_net_t *l_net = it->data;
            a_link->link_manager->callbacks.error(a_link, l_net->id, a_link->uplink.client->stage_target);
        }
        if (a_link->uplink.state == LINK_STATE_ESTABLISHED) {
            a_link->stream_is_destroyed = true;
            s_link_delete(&a_link, false, true);
        } else if (a_link->active_clusters) {
            dap_client_go_stage(a_link->uplink.client, STAGE_BEGIN, NULL);
            a_link->uplink.state = LINK_STATE_DISCONNECTED;
        }
    }
}

struct link_drop_args {
    dap_stream_node_addr_t addr;
    bool disconnected;
};

bool s_link_drop_callback(void *a_arg)
{
    struct link_drop_args *l_args = a_arg;
    // No lock needed - this runs in s_query_thread sequentially
    dap_link_t *l_link = s_link_manager_link_find(&l_args->addr);
    if (l_link)
        s_link_drop(l_link, l_args->disconnected);
    
    DAP_DEL_Z(l_args); //cause it was allocated in  s_client_error_callback
    return false;
}

/**
 * @brief s_client_error_callback
 * @param a_client
 * @param a_arg
 */
void s_client_error_callback(dap_client_t *a_client, void *a_arg)
{
    dap_return_if_pass(!a_client || !DAP_LINK(a_client));       
    dap_link_t *l_link = DAP_LINK(a_client);
    assert(l_link->uplink.client == a_client);
    struct link_drop_args *l_args = DAP_NEW_Z_RET_IF_FAIL(struct link_drop_args);
    *l_args = (struct link_drop_args) { .addr = l_link->addr, .disconnected = a_arg };
    dap_proc_thread_callback_add_pri(s_query_thread, s_link_drop_callback, l_args, DAP_QUEUE_MSG_PRIORITY_HIGH);
}

/**
 * @brief Memory free from link !!!hash table should be locked!!!
 * @param a_link - link to delet
 * @param a_force - only del dynamic, if true - all links types memory free
 */
void s_link_delete(dap_link_t **a_link, bool a_force, bool a_client_preserve)
{
// sanity check
    dap_return_if_pass(!a_link || !*a_link);
    dap_link_t *l_link = *a_link;
// func work
    debug_if(s_debug_more, L_DEBUG, "%seleting link %s node " NODE_ADDR_FP_STR "", a_force ? "Force d" : "D",
                l_link->is_uplink || !l_link->active_clusters ? "to" : "from", NODE_ADDR_FP_ARGS_S(l_link->addr));

    if (l_link->active_clusters){
        dap_cluster_link_delete_from_all(l_link->active_clusters, &l_link->addr);
        if (l_link->is_uplink && l_link->link_manager->callbacks.link_count_changed){
            for(dap_list_t *it=l_link->uplink.associated_nets;it;it=it->next){
                dap_managed_net_t *l_net = it->data;
                l_link->link_manager->callbacks.link_count_changed();
            }
        } 
    }
        
    assert(l_link->active_clusters == NULL);

    bool l_link_preserve = (a_client_preserve || l_link->static_clusters) && !a_force;
    if (!l_link->stream_is_destroyed || !l_link_preserve) {
        // Drop uplink
        dap_events_socket_uuid_t l_client_uuid = 0;
        if (l_link->uplink.client) {
            l_client_uuid = l_link->uplink.es_uuid;
            if (l_link->uplink.associated_nets) {
                dap_list_free(l_link->uplink.associated_nets);
                l_link->uplink.associated_nets = NULL;
            }
            if (l_link_preserve) {
                if (l_link->uplink.state != LINK_STATE_DISCONNECTED) {
                    dap_client_go_stage(l_link->uplink.client, STAGE_BEGIN, NULL);
                    l_link->uplink.state = LINK_STATE_DISCONNECTED;
                }
            } else
                dap_client_delete_mt(l_link->uplink.client);
        }
        // Drop downlinks if any
        dap_list_t *l_connections_for_addr = dap_stream_find_all_by_addr(&l_link->addr);
        for (dap_list_t *it = l_connections_for_addr; it; it = it->next) {
            dap_events_socket_uuid_ctrl_t *l_uuid_ctrl = it->data;
            if (l_uuid_ctrl->uuid != l_client_uuid)
                dap_events_socket_remove_and_delete_mt(l_uuid_ctrl->worker, l_uuid_ctrl->uuid);
        }
        dap_list_free_full(l_connections_for_addr, NULL);
    }
    
    if (l_link_preserve)
        return;

    dap_list_free(l_link->uplink.associated_nets);
    dap_list_free(l_link->static_clusters);
    HASH_DEL(s_link_manager->links, l_link);
    DAP_DEL_Z(*a_link);
    if (s_debug_more)
        s_link_manager_print_links_info(s_link_manager);
}

static bool s_link_have_clusters_enabled(dap_link_t *a_link)
{
    for (dap_list_t *it = a_link->static_clusters; it; it = it->next)
        if (((dap_cluster_t *)it->data)->status == DAP_CLUSTER_STATUS_ENABLED)
            return true;
    return false;
}

static void s_link_connect(dap_link_t *a_link)
{
    a_link->uplink.state = LINK_STATE_CONNECTING;
    log_it(L_INFO, "Connecting to node " NODE_ADDR_FP_STR ", addr %s : %d", NODE_ADDR_FP_ARGS_S(a_link->uplink.client->link_info.node_addr),
                                    a_link->uplink.client->link_info.uplink_addr, a_link->uplink.client->link_info.uplink_port);
    dap_client_go_stage(a_link->uplink.client, STAGE_STREAM_STREAMING, s_client_connected_callback);
}

/**
 * @brief Check existed links
 */
void s_links_wake_up(dap_link_manager_t *a_link_manager)
{
    dap_time_t l_now = dap_time_now();
    // No lock needed - this runs in s_query_thread via timer callback
    dap_link_t *it, *tmp;
    HASH_ITER(hh, a_link_manager->links, it, tmp) {
        if (!it->uplink.client)
            continue;
        if (a_link_manager->callbacks.connected &&
                it->uplink.state == LINK_STATE_ESTABLISHED &&
                it->uplink.start_after < l_now) {
            for (dap_list_t *l_net_item = it->uplink.associated_nets;
                 l_net_item;
                 l_net_item = l_net_item->next) {
                dap_managed_net_t *l_net = l_net_item->data;
                if (!dap_cluster_member_find_unsafe((dap_cluster_t *)l_net->link_clusters->data,
                                               &it->addr))
                    a_link_manager->callbacks.connected(it, l_net->id);
            }
        }
        if (it->active_clusters) {
            continue;
        }
        if (it->uplink.state != LINK_STATE_DISCONNECTED)
            continue;
        if (!it->uplink.associated_nets && !s_link_have_clusters_enabled(it))
            continue;
        if (it->uplink.start_after >= l_now)
            continue;
        if (dap_client_get_stage(it->uplink.client) != STAGE_BEGIN) {
            dap_client_go_stage(it->uplink.client, STAGE_BEGIN, NULL);
            debug_if(s_debug_more, L_ERROR, "Client " NODE_ADDR_FP_STR " state is not BEGIN, connection will start on next iteration",
                                                    NODE_ADDR_FP_ARGS_S(it->addr));
            
            continue;
        }
        int rc = s_link_manager->callbacks.fill_net_info(it);
        if (rc) {
            if (it->uplink.client->link_info.uplink_port)
                s_link_connect(it);
            else {
                log_it(L_WARNING, "Can't find node " NODE_ADDR_FP_STR " in node list and have no predefined data for it, can't connect",
                                            NODE_ADDR_FP_ARGS_S(it->addr));
                s_link_drop(it, true);
            }
        }
    }
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
        if (l_net->active ) {
            l_net->uplinks = dap_link_manager_links_count(l_net->id);
            if (a_link_manager->callbacks.link_request && l_net->uplinks < l_net->min_links_num)
                    a_link_manager->callbacks.link_request(l_net->id);
        }
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
    dap_return_if_pass_err(!s_link_manager, s_init_error);
    assert(a_arg == s_link_manager);
    dap_link_manager_t *l_link_manager = a_arg;
    // if inactive return
    if (!l_link_manager->active)
        return;
    // static mode switcher
    static bool l_wakeup_mode = false;
    if (l_wakeup_mode)
        s_links_wake_up(l_link_manager);
    else
        s_links_request(l_link_manager);
    l_wakeup_mode = !l_wakeup_mode;
}

/**
 * @brief create a link, if a_addr is NULL - it's not updated
 * @param a_node_addr - node addr to adding
 * @return if ERROR null, other - pointer to dap_link_t
 */
static dap_link_t *s_link_manager_link_create(dap_stream_node_addr_t *a_node_addr, bool a_with_client, uint64_t a_associated_net_id)
{
    dap_link_t *l_link = s_link_manager_link_find(a_node_addr);
    bool l_link_created = false;
    if (!l_link) {
        l_link = DAP_NEW_Z(dap_link_t);
        if (!l_link) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            return NULL;
        }
        debug_if(s_debug_more, L_NOTICE, "Create new link to node " NODE_ADDR_FP_STR "", NODE_ADDR_FP_ARGS(a_node_addr));
        l_link->addr.uint64 = a_node_addr->uint64;
        l_link->link_manager = s_link_manager;
        HASH_ADD(hh, s_link_manager->links, addr, sizeof(*a_node_addr), l_link);
        l_link_created = true;
    }   
    if (s_debug_more)
        s_link_manager_print_links_info(s_link_manager);
    if (a_with_client) {
        if (!l_link->uplink.client) {
            l_link->uplink.client = dap_client_new(s_client_error_callback, DAP_DUP(a_node_addr));
            l_link->uplink.client->del_arg = true;
        }
        else
            debug_if(s_debug_more, L_DEBUG, "Link " NODE_ADDR_FP_STR " already have a client", NODE_ADDR_FP_ARGS(a_node_addr));
        l_link->uplink.client->_inheritor = l_link;
        if (a_associated_net_id != DAP_NET_ID_INVALID) {
            dap_managed_net_t *l_net = s_find_net_by_id(a_associated_net_id);
            if (!l_net) {
                if (l_link_created) {
                    // Clean up the newly created link if net not found
                    HASH_DEL(s_link_manager->links, l_link);
                    if (l_link->uplink.client) {
                        dap_client_delete_mt(l_link->uplink.client);
                    }
                    DAP_DELETE(l_link);
                }
                return NULL;
            }
            for (dap_list_t *it = l_link->uplink.associated_nets; it; it = it->next)
                if (((dap_managed_net_t *)it->data)->id == a_associated_net_id) {
                    debug_if(s_debug_more, L_ERROR, "Net ID 0x%" DAP_UINT64_FORMAT_x " already associated with link " NODE_ADDR_FP_STR,
                                                                a_associated_net_id, NODE_ADDR_FP_ARGS(a_node_addr));
                    if (l_link_created) {
                        // Clean up the newly created link if net already associated
                        HASH_DEL(s_link_manager->links, l_link);
                        if (l_link->uplink.client) {
                            dap_client_delete_mt(l_link->uplink.client);
                        }
                        DAP_DELETE(l_link);
                    }
                    return NULL;
                }
            l_link->uplink.associated_nets = dap_list_append(l_link->uplink.associated_nets, l_net);
        }
    }
    return l_link;
}

DAP_STATIC_INLINE dap_link_t *s_link_manager_downlink_create(dap_stream_node_addr_t *a_node_addr)
{
    return s_link_manager_link_create(a_node_addr, false, DAP_NET_ID_INVALID);
}

struct link_create_args {
    dap_stream_node_addr_t addr;
    uint64_t associated_net_id;
};

static bool s_link_create_callback(void *a_arg)
{
    struct link_create_args *l_args = a_arg;
    // No lock needed - this runs in s_query_thread sequentially
    dap_link_t *l_link = s_link_manager_link_create(&l_args->addr, true, l_args->associated_net_id);
    s_node_hot_list_add(l_args->addr, l_args->associated_net_id);
    DAP_DELETE(l_args);
    return false;
}

inline int dap_link_manager_link_create(dap_stream_node_addr_t *a_node_addr, uint64_t a_associated_net_id)
{
// sanity check
    dap_return_val_if_pass_err(!s_link_manager, -3, s_init_error);
    dap_return_val_if_pass(!a_node_addr || !a_node_addr->uint64, -5);
    if (a_node_addr->uint64 == g_node_addr.uint64)
        return -1;
    
    struct link_create_args *l_args = DAP_NEW_Z_RET_VAL_IF_FAIL(struct link_create_args, -2);
    l_args->addr = *a_node_addr;
    l_args->associated_net_id = a_associated_net_id;
    
    return dap_proc_thread_callback_add_pri(s_query_thread, s_link_create_callback, l_args, DAP_QUEUE_MSG_PRIORITY_HIGH);
}

struct link_update_args {
    dap_stream_node_addr_t addr;
    char *host;
    uint16_t port;
};

static bool s_link_update_callback(void *a_arg)
{
    struct link_update_args *l_args = a_arg;
    // No lock needed - this runs in s_query_thread sequentially
    dap_link_t *l_link = s_link_manager_link_find(&l_args->addr);
    if (!l_link) {
        log_it(L_ERROR, "Can't update state of non-managed link " NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS_S(l_args->addr));
        goto safe_ret;
    }
    if (!l_link->uplink.client) {
        log_it(L_ERROR, "Can't update state of non-client link " NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS_S(l_link->addr));
        goto safe_ret;
    }
    if (l_link->uplink.state != LINK_STATE_DISCONNECTED) {
        log_it(L_ERROR, "Can't update state of connected link " NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS_S(l_link->addr));
        l_link->uplink.ready = false;
        goto safe_ret;
    }
    dap_client_t *l_client = l_link->uplink.client;
    dap_client_set_uplink_unsafe(l_client, &l_link->addr, l_args->host, l_args->port);
    dap_client_set_is_always_reconnect(l_client, false);
    dap_client_set_active_channels_unsafe(l_client, "RCGEND");
    log_it(L_INFO, "Validate link to node " NODE_ADDR_FP_STR " with address %s : %d", NODE_ADDR_FP_ARGS_S(l_link->addr),
                                                l_link->uplink.client->link_info.uplink_addr, l_link->uplink.client->link_info.uplink_port);
    if (l_link->uplink.ready) {
        l_link->uplink.ready = false;
        s_link_connect(l_link);
    }
safe_ret:
    DAP_DELETE(l_args->host);
    DAP_DELETE(l_args);
    return false;
}

/**
 * @brief create or update link, if any arg NULL - it's not updated
 * @param a_link - updating link
 * @param a_host - host addr
 * @param a_port - host port
 * @param a_force - if false update only if link have state DISCONECTED
 * @return if ERROR null, other - pointer to dap_link_t
 */
int dap_link_manager_link_update(dap_stream_node_addr_t *a_node_addr, const char *a_host, uint16_t a_port)
{
// sanity check
    dap_return_val_if_pass(!a_node_addr, -1);
    if (!a_host || !a_port || !strcmp(a_host, "::")) {
        log_it(L_ERROR, "Incomplete link info for uplink update");
        return -2;
    }
    struct sockaddr_storage l_numeric_addr;
    if ( 0 > dap_net_resolve_host(a_host, dap_itoa(a_port), false, &l_numeric_addr, NULL) ) {
        log_it(L_ERROR, "Wrong uplink address '%s : %u'", a_host, a_port);
        return -6;
    }
    switch (l_numeric_addr.ss_family) {
    case PF_INET: {
        unsigned long l_addr = ((struct sockaddr_in *)&l_numeric_addr)->sin_addr.s_addr;
        if (l_addr == INADDR_LOOPBACK || l_addr == INADDR_ANY || l_addr == INADDR_NONE) {
            log_it(L_ERROR, "Wrong uplink address '%s : %u'", a_host, a_port);
            return -6;
        }
    } break;
    case PF_INET6: {
        struct in6_addr *l_addr = &((struct sockaddr_in6 *)&l_numeric_addr)->sin6_addr;
        if (!memcmp(l_addr, &in6addr_any, sizeof(struct in6_addr)) ||
                !memcmp(l_addr, &in6addr_loopback, sizeof(struct in6_addr))) {
            log_it(L_ERROR, "Wrong uplink address '%s : %u'", a_host, a_port);
            return -6;
        }
    } break;
    default:
        log_it(L_ERROR, "Wrong uplink address '%s : %u'", a_host, a_port);
        return -6;
    }

    struct link_update_args *l_args = DAP_NEW_Z_RET_VAL_IF_FAIL(struct link_update_args, -7);
    l_args->host = dap_strdup(a_host);
    if (!l_args->host) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DELETE(l_args);
        return -7;
    }
    l_args->port = a_port;
    l_args->addr = *a_node_addr;
    return dap_proc_thread_callback_add(s_query_thread, s_link_update_callback, l_args);
}

struct link_find_args {
    dap_stream_node_addr_t addr;
    uint64_t net_id;
    bool *result;
    sem_t *completion_sem;
};

static bool s_link_find_callback(void *a_arg)
{
    struct link_find_args *l_args = a_arg;
    dap_link_t *l_link = NULL;
    
    // No lock needed - this runs in s_query_thread sequentially
    HASH_FIND(hh, s_link_manager->links, &l_args->addr, sizeof(l_args->addr), l_link);
    if (l_link) {
        for (dap_list_t *it = l_link->uplink.associated_nets; it; it = it->next) {
            dap_managed_net_t *l_net = it->data;
            if (l_net->id == l_args->net_id) {
                *l_args->result = true;
                break;
            }
        }
    }
    
    // Signal completion to waiting thread
    sem_post(l_args->completion_sem);
    
    return false;
}

/**
 * @brief find a link
 * @param a_node_addr - node addr to adding
 * @return if ERROR null, other - pointer to dap_link_t
 */
bool dap_link_manager_link_find(dap_stream_node_addr_t *a_node_addr, uint64_t a_net_id)
{
    dap_return_val_if_pass_err(!s_link_manager, false, s_init_error);
    dap_return_val_if_pass(!a_node_addr || !a_node_addr->uint64, false);
    
    bool l_result = false;
    
    struct link_find_args *l_args = DAP_NEW_Z_RET_VAL_IF_FAIL(struct link_find_args, false);
    l_args->addr = *a_node_addr;
    l_args->net_id = a_net_id;
    l_args->result = &l_result;

    if ( dap_proc_thread_get_current() == s_query_thread ) {
        s_link_find_callback(l_args);
        DAP_DELETE(l_args);
    } else {
        sem_t l_completion_sem;
    
        if (sem_init(&l_completion_sem, 0, 0) != 0) {
            log_it(L_ERROR, "Can't create semaphore for link find");
            DAP_DELETE(l_args);
            return false;
        }
        l_args->completion_sem = &l_completion_sem;
        
        dap_proc_thread_callback_add_pri(s_query_thread, s_link_find_callback, l_args, DAP_QUEUE_MSG_PRIORITY_HIGH);
        
        // Wait for completion with timeout
        
        int l_wait_result = sem_wait(&l_completion_sem);
        // Cleanup
        sem_destroy(&l_completion_sem);
        DAP_DELETE(l_args);
        
        if (l_wait_result != 0) {
            log_it(L_WARNING, "Link find operation timeout");
            return false;
        }
    }
    
    return l_result;
}

struct link_moving_args {
    dap_stream_node_addr_t addr;
    bool uplink;
};

static bool s_stream_add_callback(void *a_arg)
{
    assert(a_arg);
    struct link_moving_args *l_args = a_arg;
    dap_stream_node_addr_t *l_node_addr = &l_args->addr;
    // No lock needed - this runs in s_query_thread sequentially
    dap_link_t *l_link = s_link_manager_link_find(l_node_addr);
    if (!l_link && !l_args->uplink)
        l_link = s_link_manager_downlink_create(l_node_addr);
    if (!l_link) {
        log_it(L_ERROR, "Can't %s link for address " NODE_ADDR_FP_STR,
                 l_args->uplink ? "find" : "create", NODE_ADDR_FP_ARGS(l_node_addr));
        
        DAP_DELETE(l_args);
        return false;
    }
    if (l_link->active_clusters) {
        log_it(L_ERROR, "%s " NODE_ADDR_FP_STR " with existed link",
                        l_args->uplink ? "Set uplink to" : "Get dowlink from", NODE_ADDR_FP_ARGS(l_node_addr));
        DAP_DELETE(l_args);
        return false;
    }
    dap_list_t *l_item = NULL;
    DL_FOREACH(l_link->static_clusters, l_item) {
        dap_cluster_t *l_cluster = l_item->data;
        if (l_cluster->status == DAP_CLUSTER_STATUS_ENABLED){
            dap_cluster_member_add(l_cluster, l_node_addr, 0, NULL);       
            if (l_link->link_manager->callbacks.link_count_changed){
                l_link->link_manager->callbacks.link_count_changed();
            }
        }
    }
    if (l_args->uplink) {
        for (dap_list_t *it = l_link->uplink.associated_nets; it; it = it->next) {
            // if dynamic link, add net cluster to list and call callback
            dap_managed_net_t *l_net = it->data;
            if (l_net && l_net->active) {
                if (l_link->link_manager->callbacks.connected)
                    l_link->link_manager->callbacks.connected(l_link, l_net->id);
            }
        }
    }
    l_link->is_uplink = l_args->uplink;
    log_it(L_INFO, "%s " NODE_ADDR_FP_STR, l_args->uplink ? "Set uplink to" : "Get dowlink from", NODE_ADDR_FP_ARGS(l_node_addr));
    DAP_DELETE(l_args);
    return false;
}

/**
 * @brief add downlink to manager list
 * @param a_node_addr - pointer to node addr
 * @return if ok 0, other if ERROR
 */
int dap_link_manager_stream_add(dap_stream_node_addr_t *a_node_addr, bool a_uplink)
{
    dap_return_val_if_pass(!a_node_addr || !s_link_manager->active, -1);
    struct link_moving_args *l_args = DAP_NEW_Z_RET_VAL_IF_FAIL(struct link_moving_args, -2);
    *l_args = (struct link_moving_args) { .addr = *a_node_addr, .uplink = a_uplink };
    return dap_proc_thread_callback_add_pri(s_query_thread, s_stream_add_callback, l_args, DAP_QUEUE_MSG_PRIORITY_HIGH);
}

static bool s_stream_replace_callback(void *a_arg)
{
    assert(a_arg);
    struct link_moving_args *l_args = a_arg;
    dap_stream_node_addr_t *l_node_addr = &l_args->addr;
    // No lock needed - this runs in s_query_thread sequentially
    dap_link_t *l_link = s_link_manager_link_find(l_node_addr);
    if (!l_link) { // Link is not managed by us
        DAP_DELETE(l_args);
        return false;
    }
    if (!l_link->active_clusters) { // Link is managed and currently inactive
        DAP_DELETE(l_args);
        return false;
    }
    if (l_link->is_uplink && !l_args->uplink) {
        // Have downlink from same addr, stop the client therefore
        assert(l_link->uplink.client);
        dap_client_go_stage(l_link->uplink.client, STAGE_BEGIN, NULL);
        l_link->uplink.state = LINK_STATE_DISCONNECTED;
    }
    l_link->is_uplink = l_args->uplink;
    DAP_DELETE(l_args);
    return false;
}

void dap_link_manager_stream_replace(dap_stream_node_addr_t *a_addr, bool a_new_is_uplink)
{
    dap_return_if_fail(a_addr);
    struct link_moving_args *l_args = DAP_NEW_Z_RET_IF_FAIL(struct link_moving_args);
    *l_args = (struct link_moving_args) { .addr = *a_addr, .uplink = a_new_is_uplink };
    dap_proc_thread_callback_add_pri(s_query_thread, s_stream_replace_callback, l_args, DAP_QUEUE_MSG_PRIORITY_HIGH);
}

static bool s_stream_delete_callback(void *a_arg)
{
    assert(a_arg);
    dap_stream_node_addr_t *l_node_addr = a_arg;
    // No lock needed - this runs in s_query_thread sequentially
    dap_link_t *l_link = s_link_manager_link_find(l_node_addr);
    if (!l_link) {
        DAP_DELETE(a_arg);
        return false; // It's OK if stream is uregistered with us
    }
    if (!l_link->active_clusters) {
        DAP_DELETE(a_arg);
        return false; // It's OK if net is unregistered yet
    }
    l_link->stream_is_destroyed = true;
    dap_cluster_link_delete_from_all(l_link->active_clusters, l_node_addr);
    if (l_link->link_manager->callbacks.link_count_changed){
        l_link->link_manager->callbacks.link_count_changed();
    }
    if (!l_link->uplink.client)
        s_link_delete(&l_link, false, false);
    DAP_DELETE(a_arg);
    return false;
}

void dap_link_manager_stream_delete(dap_stream_node_addr_t *a_node_addr)
{
    dap_return_if_fail(a_node_addr);
    dap_stream_node_addr_t *l_args = DAP_DUP(a_node_addr);
    if (!l_args) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return;
    }
    dap_proc_thread_callback_add_pri(s_query_thread, s_stream_delete_callback, l_args, DAP_QUEUE_MSG_PRIORITY_HIGH);
}

struct link_accounting_args {
    dap_stream_node_addr_t addr;
    dap_managed_net_t *net;
    bool no_error;
};

static bool s_link_accounting_callback(void *a_arg)
{
    struct link_accounting_args *l_args = a_arg;
    dap_stream_node_addr_t *l_node_addr = &l_args->addr;
    dap_managed_net_t *l_net = l_args->net;
    // No lock needed - this runs in s_query_thread sequentially
    dap_link_t *l_link = s_link_manager_link_find(l_node_addr);
    if (!l_link) { // No link to accounting. May be it was already deleted
        DAP_DELETE(l_args);
        return false;
    }
    if (l_args->no_error) {
        assert(l_net && l_net->active);
        for (dap_list_t *it = l_net->link_clusters; it; it = it->next) {
            dap_cluster_t *l_cluster = it->data;
            if (it == l_net->link_clusters){
                dap_cluster_member_add(l_cluster, l_node_addr, 0, NULL);
                if (l_link->link_manager->callbacks.link_count_changed){
                    l_link->link_manager->callbacks.link_count_changed();
                } 
            } else {
                for (dap_list_t *l_item = l_link->static_clusters; l_item; l_item = l_item->next) {
                    if (l_cluster == l_item->data) {
                        assert(l_cluster->status == DAP_CLUSTER_STATUS_ENABLED);
                        dap_cluster_member_add(l_cluster, l_node_addr, 0, NULL);
                        if (l_link->link_manager->callbacks.link_count_changed){
                            l_link->link_manager->callbacks.link_count_changed();
                        } 
                        break;
                    }
                }
            }
        }
        s_debug_accounting_link_in_net(l_link->is_uplink, l_node_addr, l_net->id);
    } else if (l_net) {
        assert(l_net->link_clusters);
        dap_cluster_link_delete_from_all(l_net->link_clusters, l_node_addr);
        if (l_link->link_manager->callbacks.link_count_changed){
            l_link->link_manager->callbacks.link_count_changed();
        }
        l_link->uplink.start_after = dap_time_now() + l_link->link_manager->reconnect_delay;
        if (l_link->link_manager->callbacks.disconnected) {
            bool l_is_permanent_link = l_link->link_manager->callbacks.disconnected(
                        l_link, l_net->id, dap_cluster_members_count((dap_cluster_t *)l_net->link_clusters->data));
            if (l_is_permanent_link) {
                DAP_DELETE(l_args);
                return false;
            }
        }
        l_link->uplink.associated_nets = dap_list_remove(l_link->uplink.associated_nets, l_net);
        if (l_link->uplink.client && !l_link->uplink.associated_nets && !l_link->static_clusters)
            s_link_delete(&l_link, false, false);
    }
    DAP_DELETE(l_args);
    return false;
}

/**
 * @brief used to add member to link cluster if stream established
 * @param a_net_id - net id to check
 * @param a_node_addr - node addr to check
 */
void dap_link_manager_accounting_link_in_net(uint64_t a_net_id, dap_stream_node_addr_t *a_node_addr, bool a_no_error)
{
    dap_managed_net_t *l_net = s_find_net_by_id(a_net_id);
    dap_return_if_pass(!l_net);
    struct link_accounting_args *l_args = DAP_NEW_Z_RET_IF_FAIL(struct link_accounting_args);
    *l_args = (struct link_accounting_args) { .addr = *a_node_addr, .net = l_net, .no_error = a_no_error };
    dap_proc_thread_callback_add_pri(s_query_thread, s_link_accounting_callback, l_args, DAP_QUEUE_MSG_PRIORITY_NORMAL);
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

struct static_cluster_args {
    dap_stream_node_addr_t addr;
    dap_cluster_t *cluster;
    bool adding;
};

static bool s_static_cluster_callback(void *a_arg)
{
    struct static_cluster_args *l_args = a_arg;
    
    if (l_args->addr.uint64 == g_node_addr.uint64) {
        DAP_DELETE(l_args);
        return false; // without error
    }
    
    // No lock needed - this runs in s_query_thread sequentially
    dap_link_t *l_link = s_link_manager_link_find(&l_args->addr);
    if (!l_link && l_args->adding)
        l_link = s_link_manager_link_create(&l_args->addr, true, DAP_NET_ID_INVALID);
    
    if (!l_link) {
        if (l_args->adding)
            log_it(L_ERROR, "Can't create link to addr " NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS(&l_args->addr));
        else
            debug_if(s_debug_more, L_ERROR, "Link " NODE_ADDR_FP_STR " not found", NODE_ADDR_FP_ARGS(&l_args->addr));
        DAP_DELETE(l_args);
        return false;
    }
    
    if (l_args->adding) {
        l_link->static_clusters = dap_list_append(l_link->static_clusters, l_args->cluster);
    } else {
        l_link->static_clusters = dap_list_remove(l_link->static_clusters, l_args->cluster);
        if (!l_link->static_clusters && !l_link->active_clusters)
            s_link_delete(&l_link, false, true);
    }
    
    s_debug_cluster_adding_removing(true, l_args->adding, l_args->cluster, &l_args->addr);
    DAP_DELETE(l_args);
    return false;
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
    struct static_cluster_args *l_args = DAP_NEW_Z_RET_IF_FAIL(struct static_cluster_args);
    l_args->addr = a_member->addr;
    l_args->cluster = (dap_cluster_t *)a_arg;
    l_args->adding = true;
    
    dap_proc_thread_callback_add_pri(s_query_thread, s_static_cluster_callback, l_args, DAP_QUEUE_MSG_PRIORITY_HIGH);
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
    struct static_cluster_args *l_args = DAP_NEW_Z_RET_IF_FAIL(struct static_cluster_args);
    l_args->addr = a_member->addr;
    l_args->cluster = (dap_cluster_t *)a_arg;
    l_args->adding = false;
    
    dap_proc_thread_callback_add_pri(s_query_thread, s_static_cluster_callback, l_args, DAP_QUEUE_MSG_PRIORITY_HIGH);
}

struct get_links_addrs_args {
    uint64_t net_id;
    bool established_only;
    dap_stream_node_addr_t **result;
    size_t *uplinks_count;
    size_t *downlinks_count;
    sem_t *completion_sem;
};

static bool s_get_net_links_addrs_callback(void *a_arg)
{
    struct get_links_addrs_args *l_args = a_arg;
    dap_managed_net_t *l_net = s_find_net_by_id(l_args->net_id);
    
    if (!l_net || !l_net->link_clusters) {
        *l_args->result = NULL;
        *l_args->uplinks_count = 0;
        *l_args->downlinks_count = 0;
        goto cleanup;
    }
    
    size_t l_uplinks_count = 0, l_downlinks_count = 0;
    dap_stream_node_addr_t *l_ret = NULL;
    
    // No lock needed - this runs in s_query_thread sequentially
    size_t i, l_cur_count = 0;
    dap_stream_node_addr_t *l_links_addrs = dap_cluster_get_all_members_addrs((dap_cluster_t *)l_net->link_clusters->data, &l_cur_count, -1);
    if (l_cur_count) {
        l_ret = DAP_NEW_Z_COUNT_RET_VAL_IF_FAIL(dap_stream_node_addr_t, l_cur_count, NULL);
        for (i = 0; i < l_cur_count; ++i) {
            dap_link_t *l_link = NULL;
            HASH_FIND(hh, s_link_manager->links, l_links_addrs + i, sizeof(l_links_addrs[i]), l_link);
            if (!l_link || (l_link->is_uplink && l_args->established_only && l_link->uplink.state != LINK_STATE_ESTABLISHED)) {
                continue;
            } else if (l_link->is_uplink) {  // first uplinks, second downlinks
                l_ret[l_uplinks_count + l_downlinks_count].uint64 = l_ret[l_uplinks_count].uint64;
                l_ret[l_uplinks_count].uint64 = l_link->addr.uint64;
                ++l_uplinks_count;
            } else {
                l_ret[l_uplinks_count + l_downlinks_count].uint64 = l_link->addr.uint64;
                ++l_downlinks_count;
            }
        }
        DAP_DELETE(l_links_addrs);
    } else
        log_it(L_INFO, "No links in net with ID 0x%016" DAP_UINT64_FORMAT_x, l_net->id);
    
    if (!l_uplinks_count && !l_downlinks_count)
        DAP_DEL_Z(l_ret);
    
    *l_args->result = l_ret;
    *l_args->uplinks_count = l_uplinks_count;
    *l_args->downlinks_count = l_downlinks_count;

cleanup:
    // Signal completion to waiting thread
    sem_post(l_args->completion_sem);
    
    return false;
}

/**
 * @brief forming list with active links addrs
 * @param a_net_id net id to search
 * @param a_only_uplinks if TRUE count only uplinks
 * @param a_uplinks_count output count of finded uplinks
 * @param a_downlinks_count output count of finded downlinks
 * @return pointer to dap_stream_node_addr_t array, first uplinks, second downlinks, or NULL
 */
dap_stream_node_addr_t *dap_link_manager_get_net_links_addrs(uint64_t a_net_id, size_t *a_uplinks_count, size_t *a_downlinks_count, bool a_established_only)
{
    dap_stream_node_addr_t *l_result = NULL;
    size_t l_uplinks_count = 0, l_downlinks_count = 0;

    struct get_links_addrs_args *l_args = DAP_NEW_Z_RET_VAL_IF_FAIL(struct get_links_addrs_args, NULL);
    l_args->net_id = a_net_id;
    l_args->established_only = a_established_only;
    l_args->result = &l_result;
    l_args->uplinks_count = &l_uplinks_count;
    l_args->downlinks_count = &l_downlinks_count;
    if ( dap_proc_thread_get_current() == s_query_thread ) {
        s_get_net_links_addrs_callback(l_args);
        DAP_DELETE(l_args);
    } else {
        sem_t l_completion_sem;
        if (sem_init(&l_completion_sem, 0, 0) != 0) {
            log_it(L_ERROR, "Can't create semaphore for get net links");
            DAP_DELETE(l_args);
            return NULL;
        }
        
        l_args->completion_sem = &l_completion_sem;
        
        dap_proc_thread_callback_add_pri(s_query_thread, s_get_net_links_addrs_callback, l_args, DAP_QUEUE_MSG_PRIORITY_HIGH);
        
        // Wait for completion with timeout
        
        int l_wait_result = sem_wait(&l_completion_sem);
        
        // Cleanup
        sem_destroy(&l_completion_sem);
        DAP_DELETE(l_args);
        if (l_wait_result != 0) {
            log_it(L_WARNING, "Get net links operation timeout");
            if (a_uplinks_count) *a_uplinks_count = 0;
            if (a_downlinks_count) *a_downlinks_count = 0;
            return NULL;
        }
    }
    
    if (a_uplinks_count)
        *a_uplinks_count = l_uplinks_count;
    if (a_downlinks_count)
        *a_downlinks_count = l_downlinks_count;
    
    return l_result;
}

/**
 * @brief forming list with ignored addrs
 * @param a_ignored_count output count of finded addrs
 * @return pointer to dap_stream_node_addr_t array or NULL
 */
dap_stream_node_addr_t *dap_link_manager_get_ignored_addrs(size_t *a_ignored_count, uint64_t a_net_id)
{
    if(s_update_hot_list(a_net_id))
        return NULL;
    size_t l_node_count = 0;
    char *l_hot_group = s_hot_group_forming(a_net_id);
    dap_global_db_obj_t *l_objs = dap_global_db_get_all_sync(l_hot_group, &l_node_count);
    DAP_DEL_Z(l_hot_group);
    if (!l_node_count || !l_objs) {        
        log_it(L_DEBUG, "Hot list is empty");
        return NULL;
    }
// memory alloc
    dap_stream_node_addr_t *l_ret = DAP_NEW_Z_COUNT(dap_stream_node_addr_t, l_node_count);
    if (!l_ret) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        dap_global_db_objs_delete(l_objs, l_node_count);
        return NULL;
    }
// func work
    for (size_t i = 0; i < l_node_count; ++i)
        dap_stream_node_addr_from_str(l_ret + i, l_objs[i].key);
    dap_global_db_objs_delete(l_objs, l_node_count);
    if (a_ignored_count)
        *a_ignored_count = l_node_count;
    return l_ret;
}