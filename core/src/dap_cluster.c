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
#include "dap_cluster.h"

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
dap_cluster_t *dap_cluster_new(dap_cluster_options_t *a_options)
{
    dap_cluster_t *l_ret = DAP_NEW_Z(dap_cluster_t);
    if (!l_ret) {
        log_it(L_CRITICAL, "Insufficient memory");
        return NULL;
    }
    pthread_rwlock_init(&l_ret->members_lock, NULL);
    l_ret->options = a_options;
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
    DAP_DEL_Z(a_cluster->options);
    assert(!a_cluster->_inheritor);
    DAP_DELETE(a_cluster);
}

/**
 * @brief dap_cluster_member_add
 * @param a_cluster
 * @param a_member
 * @return
 */
dap_cluster_member_t *dap_cluster_member_add(dap_cluster_t *a_cluster, dap_stream_node_addr_t *a_addr, dap_cluster_role_t a_role, void *a_info)
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
        .addr = *a_addr,
        .cluster = a_cluster,
        .role = a_role,
        .info = a_info
    };
    HASH_ADD(hh, a_cluster->members, addr, sizeof(*a_addr), l_member);
    pthread_rwlock_unlock(&a_cluster->members_lock);
    if (l_member->cluster->members_callback)
        l_member->cluster->members_callback(a_cluster, l_member, DAP_CLUSTER_MEMBER_ADD);
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
    if (a_member->cluster->members_callback)
        a_member->cluster->members_callback(a_member->cluster, a_member, DAP_CLUSTER_MEMBER_DELETE);
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
dap_cluster_member_t *dap_cluster_member_find(dap_cluster_t *a_cluster, dap_stream_node_addr_t *a_member_addr)
{
    dap_cluster_member_t *l_member = NULL;
    pthread_rwlock_rdlock(&a_cluster->members_lock);
    HASH_FIND(hh, a_cluster->members, a_member_addr, sizeof(*a_member_addr), l_member);
    pthread_rwlock_unlock(&a_cluster->members_lock);
    return l_member;
}