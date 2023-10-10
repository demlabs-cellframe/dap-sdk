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
#include "dap_stream_cluster.h"
#include "dap_list.h"
#include "dap_events_socket.h"
#include "dap_stream_worker.h"
#include "dap_stream_ch_pkt.h"

#define LOG_TAG "dap_cluster"

dap_cluster_t * s_clusters = NULL;
pthread_rwlock_t s_clusters_rwlock = PTHREAD_RWLOCK_INITIALIZER;

static void s_cluster_member_delete(dap_cluster_member_t *a_member);

dap_cluster_t *dap_cluster_find(dap_guuid_t a_cluster_id)
{
    dap_cluster_t *l_ret = NULL;
    pthread_rwlock_rdlock(&s_clusters_rwlock);
    HASH_FIND(hh, s_clusters, &a_cluster_id, sizeof(dap_guuid_t), l_ret);
    pthread_rwlock_unlock(&s_clusters_rwlock);
    return l_ret;
}

/**
 * @brief dap_cluster_new
 * @param a_options
 * @return
 */
dap_cluster_t *dap_cluster_new(dap_cluster_role_t a_role)
{
    dap_cluster_t *l_ret = DAP_NEW_Z(dap_cluster_t);
    if (!l_ret) {
        log_it(L_CRITICAL, "Insufficient memory");
        return NULL;
    }
    pthread_rwlock_init(&l_ret->members_lock, NULL);
    l_ret->role = a_role;
    dap_cluster_t *l_check = NULL;
    do {
        l_ret->guuid = dap_guuid_new();
        l_check = dap_cluster_find(l_ret->guuid);
    } while (l_check);
    pthread_rwlock_wrlock(&s_clusters_rwlock);
    HASH_ADD_KEYPTR(hh, s_clusters, &l_ret->guuid, sizeof(l_ret->guuid), l_ret);
    pthread_rwlock_unlock(&s_clusters_rwlock);
    return l_ret;
}

/**
 * @brief dap_cluster_delete
 * @param a_cluster
 */
void dap_cluster_delete(dap_cluster_t *a_cluster)
{
    if (!a_cluster)
        return;
    pthread_rwlock_wrlock(&s_clusters_rwlock);
    HASH_DEL(s_clusters, a_cluster);
    pthread_rwlock_unlock(&s_clusters_rwlock);
    dap_cluster_member_t *l_member, *l_tmp;
    pthread_rwlock_wrlock(&a_cluster->members_lock);
    HASH_ITER(hh, a_cluster->members, l_member, l_tmp)
        s_cluster_member_delete(l_member);
    pthread_rwlock_unlock(&a_cluster->members_lock);
    assert(!a_cluster->_inheritor);
    DAP_DELETE(a_cluster);
}

/**
 * @brief dap_cluster_member_add
 * @param a_cluster
 * @param a_member
 * @return
 */
dap_cluster_member_t *dap_cluster_member_add(dap_cluster_t *a_cluster, dap_stream_node_addr_t *a_addr, int a_role, void *a_info)
{
    dap_cluster_member_t *l_member = NULL;
    pthread_rwlock_wrlock(&a_cluster->members_lock);
    HASH_FIND(hh,a_cluster->members, a_addr, sizeof(*a_addr), l_member);
    if (l_member) {
        pthread_rwlock_unlock(&a_cluster->members_lock);
        log_it(L_WARNING, "Trying to add member "NODE_ADDR_FP_STR" but its already present in cluster ",
                                                NODE_ADDR_FP_ARGS(a_addr));
        return NULL;
    }
    l_member = DAP_NEW_Z(dap_cluster_member_t);
    if (!l_member) {
        log_it(L_CRITICAL, "Insufficient memory");
        return NULL;
    }
    *l_member = (dap_cluster_member_t) {
        .addr       = *a_addr,
        .cluster    = a_cluster,
        .role       = a_role,
        .info       = a_info
    };
    HASH_ADD(hh, a_cluster->members, addr, sizeof(*a_addr), l_member);
    pthread_rwlock_unlock(&a_cluster->members_lock);
    if (l_member->cluster->members_add_callback)
        l_member->cluster->members_add_callback(a_cluster, l_member);
    return l_member;
}

/**
 * @brief dap_cluster_member_delete
 * @param a_member
 */
void dap_cluster_member_delete(dap_cluster_member_t *a_member)
{
    pthread_rwlock_wrlock(&a_member->cluster->members_lock);
    s_cluster_member_delete(a_member);
    pthread_rwlock_unlock(&a_member->cluster->members_lock);
}

static void s_cluster_member_delete(dap_cluster_member_t *a_member)
{
    if (a_member->cluster->members_delete_callback)
        a_member->cluster->members_delete_callback(a_member->cluster, a_member);
    HASH_DEL(a_member->cluster, a_member);
    DAP_DEL_Z(a_member->info);
    DAP_DELETE(a_member);
}

/**
 * @brief dap_cluster_member_find
 * @param a_cluster
 * @param a_member_id
 * @return
 */
dap_cluster_member_t *dap_cluster_member_find_unsafe(dap_cluster_t *a_cluster, dap_stream_node_addr_t *a_member_addr)
{
    dap_cluster_member_t *l_member = NULL;
    pthread_rwlock_rdlock(&a_cluster->members_lock);
    HASH_FIND(hh, a_cluster->members, a_member_addr, sizeof(*a_member_addr), l_member);
    pthread_rwlock_unlock(&a_cluster->members_lock);
    return l_member;
}

int dap_cluster_member_find_role(dap_cluster_t *a_cluster, dap_stream_node_addr_t *a_member_addr)
{
    dap_cluster_member_t *l_member = NULL;
    pthread_rwlock_rdlock(&a_cluster->members_lock);
    HASH_FIND(hh, a_cluster->members, a_member_addr, sizeof(*a_member_addr), l_member);
    pthread_rwlock_unlock(&a_cluster->members_lock);
    return l_member ? l_member->role : 0;
}

dap_list_t *dap_cluster_get_shuffle_addrs(dap_cluster_t *a_cluster)
{
    dap_return_val_if_fail(a_cluster, NULL);
    dap_list_t *l_ret = NULL;
    pthread_rwlock_rdlock(&a_cluster->members_lock);
    for (dap_cluster_member_t *it = a_cluster->members; it; it = it->hh.next)
        l_ret = dap_list_append(l_ret, DAP_DUP(&it->addr));
    pthread_rwlock_unlock(&a_cluster->members_lock);
    return dap_list_shuffle(l_ret);
}

static dap_list_t *dap_stream_get_shuffle_addrs() { return NULL; } // for build pass

void dap_cluster_broadcast(dap_cluster_t *a_cluster, const char a_ch_id, uint8_t a_type, const void *a_data, size_t a_data_size)
{
    dap_list_t *l_link_list = a_cluster ? dap_cluster_get_shuffle_addrs(a_cluster) : dap_stream_get_shuffle_addrs();
    int l_links_used = 0;
    for (dap_list_t *it = l_link_list; it; it = it->next) {
        dap_stream_node_addr_t *l_addr = it->data;
        assert(l_addr);
        dap_worker_t *l_worker = NULL;
        dap_events_socket_uuid_t l_uuid = dap_stream_find_by_addr(*l_addr, &l_worker);
        if (l_worker) {
            dap_stream_ch_pkt_send_mt(DAP_STREAM_WORKER(l_worker), l_uuid, a_ch_id, a_type, a_data, a_data_size);
            if (++l_links_used >= DAP_CLUSTER_OPTIMUM_LINKS)
                break;
        }
    }
    dap_list_free_full(l_link_list, NULL);
}

char *dap_cluster_get_links_info(dap_cluster_t *a_cluster)
{
    dap_stream_info_t l_link_info;
    pthread_rwlock_rdlock(&a_cluster->members_lock);
    for (dap_cluster_member_t *it = a_cluster->members; it; it = it->hh.next) {
        if (!dap_stream_get_link_info(it->addr, &l_link_info)) {

    pthread_rwlock_unlock(&a_cluster->members_lock);

    pthread_mutex_lock(&l_net_pvt->uplinks_mutex);
    dap_string_t *l_str_uplinks = dap_string_new("---------------------------\n"
                                         "| ↑\\↓ |\t#\t|\t\tIP\t\t|\tPort\t|\n");
    struct net_link *l_link, *l_link_tmp = NULL;
    size_t l_up_count = 0;
    HASH_ITER(hh, l_net_pvt->net_links, l_link, l_link_tmp) {
        dap_string_append_printf(l_str_uplinks, "|  ↑  |\t%zu\t|\t%s\t\t|\t%u\t|\n",
                                 ++l_up_count,
                                 inet_ntoa(l_link->link_info->hdr.ext_addr_v4),
                                 l_link->link_info->hdr.ext_port);
    }


    size_t l_down_count = 0;
    dap_string_t *l_str_downlinks = dap_string_new("---------------------------\n"
                                                 "| ↑\\↓ |\t#\t|\t\tIP\t\t|\tPort\t|\n");
    pthread_mutex_unlock(&l_net_pvt->uplinks_mutex);
    pthread_mutex_lock(&l_net_pvt->downlinks_mutex);
    struct downlink *l_downlink = NULL, *l_downtmp = NULL;
    HASH_ITER(hh, l_net_pvt->downlinks, l_downlink, l_downtmp) {
        dap_string_append_printf(l_str_downlinks, "|  ↓  |\t%zu\t|\t%s\t\t|\t%u\t|\n",
                                     ++l_down_count,
                                     l_downlink->addr, l_downlink->port);
    }
    pthread_mutex_unlock(&l_net_pvt->downlinks_mutex);
    char *l_res_str = dap_strdup_printf("Count links: %zu\n\nUplinks: %zu\n%s\n\nDownlinks: %zu\n%s\n",
                                        l_up_count + l_down_count, l_up_count, l_str_uplinks->str,
                                        l_down_count, l_str_downlinks->str);
    dap_string_free(l_str_uplinks, true);
    dap_string_free(l_str_downlinks, true);
}
