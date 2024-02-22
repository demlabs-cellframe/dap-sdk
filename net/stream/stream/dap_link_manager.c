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

static const char *s_init_error = "Link manager not inited";
static uint32_t s_timer_update_states = 4000;
static uint32_t s_min_links_num = 5;
static uint32_t s_max_attempts_num = 5;
static dap_link_manager_t *s_link_manager = NULL;

static void s_client_connect(dap_link_t *a_link, void *a_callback_arg);
static void s_client_connected_callback(dap_client_t *a_client, void *a_arg);
static void s_client_error_callback(dap_client_t *a_client, void *a_arg);
static void s_client_delete_callback(UNUSED_ARG dap_client_t *a_client, void *a_arg);
static void s_accounting_link_in_net(dap_link_t *a_link, dap_managed_net_t *a_net);
static void s_link_delete(dap_link_t *a_link);
static bool s_check_active_nets();
static void s_links_wake_up();
static void s_links_request();
static bool s_update_states(void *a_arg);

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
        log_it(L_INFO, "Connecting to node" NODE_ADDR_FP_STR ", addr %s : %d", NODE_ADDR_FP_ARGS_S(a_link->node_addr), a_link->client->uplink_addr, a_link->client->uplink_port);
        a_link->state = LINK_STATE_CONNECTING ;
        dap_client_go_stage(a_link->client, STAGE_STREAM_STREAMING, s_client_connected_callback);
    } else if (a_callback_arg && a_link->state == LINK_STATE_ESTABLISHED) {
        s_accounting_link_in_net(a_link, (dap_managed_net_t *)a_callback_arg);
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
    dap_link_t *l_link = DAP_LINK(a_client);
    dap_return_if_pass(!l_link);
// func work
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
                l_link->client->uplink_addr, l_link->client->uplink_port);
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
    dap_link_t *l_link = DAP_LINK(a_client);
    dap_return_if_pass(!l_link);
// func work
    // check for last attempt
    bool l_is_last_attempt = a_arg ? true : false;
    uint64_t l_net_id = a_client->callbacks_arg ? ((dap_managed_net_t *)(a_client->callbacks_arg))->id : 0;
    if (l_is_last_attempt) {
        l_link->state = LINK_STATE_DISCONNECTED;

        if (l_link->link_manager->callbacks.disconnected) {
            l_link->link_manager->callbacks.disconnected(l_link, l_net_id, ((dap_managed_net_t *)(a_client->callbacks_arg))->links_count );
        }
        dap_cluster_link_delete_from_all(&l_link->node_addr);
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
    dap_link_t *l_link = DAP_LINK(a_client);
    dap_return_if_pass(!l_link);
// func work
    l_link->client = NULL;
}


/**
 * @brief used to add member to link cluster if stream established
 * @param a_link - link to check
 * @param a_net - net to check
 */
void s_accounting_link_in_net(dap_link_t *a_link, dap_managed_net_t *a_net)
{
// sanity check
    dap_return_if_pass(!a_link || !a_net);
// func work
    if (!dap_cluster_member_find_unsafe(a_net->node_link_cluster, &a_link->node_addr)) {
        dap_cluster_member_add(a_net->node_link_cluster, &a_link->node_addr, 0, NULL);
        if(a_link->link_manager->callbacks.connected)
            a_link->link_manager->callbacks.connected(a_link, a_net->id);
    }
}


/**
 * @brief Memory free from link !!!hash table should be locked!!!
 * @param a_link
 */
void s_link_delete(dap_link_t *a_link)
{
// sanity check
    dap_client_delete_mt(a_link->client);
    HASH_DEL(s_link_manager->links, a_link);
    dap_list_free(a_link->links_clusters);
    DAP_DELETE(a_link);
}


/**
 * @brief Check existed links
 */
void s_links_wake_up()
{
    dap_link_t *l_link = NULL, *l_tmp = NULL;
    // pthread_rwlock_wrlock(&a_cluster->members_lock);
    HASH_ITER(hh, s_link_manager->links, l_link, l_tmp) {
        // if disconnected try connect
        if(l_link->state == LINK_STATE_DISCONNECTED) {
            if (l_link->attempts_count >= s_link_manager->max_attempts_num) {
                s_link_delete(l_link);
            } else if (l_link->valid || !s_link_manager->callbacks.fill_net_info(l_link)) {
                s_client_connect(l_link, l_link->client->callbacks_arg);
            } else {
                log_it(L_INFO, "Can't find node "NODE_ADDR_FP_STR" in node list", NODE_ADDR_FP_ARGS_S(l_link->node_addr));
            }
            if (!l_link->keep_connection_count)
                l_link->attempts_count++;
        }
    }
    // pthread_rwlock_unlock(&a_cluster->members_lock);
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
    // if inactive remove timer
    if (!s_link_manager->active) {
        s_link_manager->update_timer = NULL;
        return false;
    }
    // static mode switcher
    static bool l_wakeup_mode = false;
    if (s_check_active_nets()) {
        if (l_wakeup_mode)
            s_links_wake_up();
        else
            s_links_request();
        l_wakeup_mode = !l_wakeup_mode;
    }
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
    dap_link_t *l_link = NULL, *l_tmp = NULL;
    dap_link_manager_set_condition(false);
    // pthread_rwlock_wrlock(&a_cluster->members_lock);
    HASH_ITER(hh, s_link_manager->links, l_link, l_tmp) {
        HASH_DEL(s_link_manager->links, l_link);
        s_link_delete(l_link);
    }
    // pthread_rwlock_unlock(&a_cluster->members_lock);
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

/**
 * @brief set active or inactive status
 * @param a_net_id - net id to set
 */
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

/**
 * @brief check adding member in role cluster
 * @param a_addr - node addr to adding
 * @param a_cluster - pointer to role cluster
 */
void dap_link_manager_add_role_cluster(dap_stream_node_addr_t *a_addr, dap_cluster_t *a_cluster)
{
    dap_return_if_pass(!s_link_manager || !a_addr || !a_cluster);
    dap_link_t *l_link = dap_link_manager_link_create_or_update(a_addr, NULL, NULL, 0);
    l_link->keep_connection_count++;
}

/**
 * @brief check adding member in links cluster
 * @param a_addr - node addr to adding
 * @param a_cluster - pointer to links cluster
 */
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

/**
 * @brief check removing member from role cluster
 * @param a_addr - node addr to adding
 * @param a_cluster - pointer to role cluster
 */
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
    l_link->keep_connection_count--;
    // pthread_rwlock_unlock(&it->members_lock);
}

/**
 * @brief check removing member from links cluster
 * @param a_addr - node addr to adding
 * @param a_cluster - pointer to links cluster
 */
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

/**
 * @brief create or update link, if any arg NULL - it's not updated
 * @param a_node_addr - node addr to adding
 * @param a_addr_v4 - pointer to in_addr
 * @param a_addr_v6 - pointer to in6_addr
 * @param a_port - host port
 * @return if ERROR null, other - pointer to dap_link_t
 */
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
        l_ret->client = dap_client_new(s_client_delete_callback, s_client_error_callback, NULL);
        dap_client_set_is_always_reconnect(l_ret->client, false);
        dap_client_set_active_channels_unsafe(l_ret->client, "CGND");
        l_ret->client->_inheritor = l_ret;
        HASH_ADD(hh, s_link_manager->links, node_addr, sizeof(l_ret->node_addr), l_ret);
    }
    // pthread_rwlock_unlock(&a_cluster->members_lock);

    // fill addr
    if(a_addr_v4 && a_addr_v4->s_addr){
        inet_ntop(AF_INET, a_addr_v4, l_ret->client->uplink_addr, INET_ADDRSTRLEN);
    } else if (a_addr_v6) {
        inet_ntop(AF_INET6, a_addr_v6, l_ret->client->uplink_addr, INET6_ADDRSTRLEN);
    }
    if (a_port)
        l_ret->client->uplink_port = a_port;
    l_ret->node_addr.uint64 = a_node_addr->uint64;
    l_ret->valid = l_ret->client->uplink_port && (strlen(l_ret->client->uplink_addr) && strcmp(l_ret->client->uplink_addr, "::"));
    if (!l_ret->valid) {
        log_it(L_INFO, "Create link to node " NODE_ADDR_FP_STR " with undefined address %s : %d", NODE_ADDR_FP_ARGS_S(l_ret->node_addr), l_ret->client->uplink_addr, l_ret->client->uplink_addr);
    } else {

    }
    return l_ret;
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
    s_client_connect(a_link, l_item->data);
    return 0;
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
    dap_return_if_pass_err(!s_link_manager, s_init_error);
// func work
    return s_link_manager->active;
}