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
#include "dap_strfuncs.h"

#define LOG_TAG "dap_cluster"

static dap_cluster_t *s_clusters = NULL, *s_cluster_mnemonims = NULL;
static pthread_rwlock_t s_clusters_rwlock = PTHREAD_RWLOCK_INITIALIZER;

static void s_cluster_member_delete(dap_cluster_member_t *a_member);

/**
 * @brief dap_cluster_new
 * @param a_options
 * @return
 */
dap_cluster_t *dap_cluster_new(const char *a_mnemonim, dap_guuid_t a_uuid, dap_cluster_role_t a_role)
{
    dap_cluster_t *l_ret = DAP_NEW_Z(dap_cluster_t);
    if (!l_ret) {
        log_it(L_CRITICAL, "Insufficient memory");
        return NULL;
    }
    pthread_rwlock_init(&l_ret->members_lock, NULL);
    l_ret->role = a_role;
    dap_cluster_t *l_check = NULL;
    pthread_rwlock_wrlock(&s_clusters_rwlock);
    if (a_mnemonim) {
        HASH_FIND(hh_str, s_cluster_mnemonims, a_mnemonim, strlen(a_mnemonim), l_check);
        if (l_check) {
            log_it(L_ERROR, "Mnemonim %s already in use", a_mnemonim);
            DAP_DELETE(l_ret);
            return NULL;
        }
        HASH_ADD_KEYPTR(hh_str, s_cluster_mnemonims, a_mnemonim, strlen(a_mnemonim), l_ret);
    }
    if (!IS_ZERO_128(a_uuid.raw)) {
        HASH_FIND(hh, s_clusters, &a_uuid, sizeof(dap_guuid_t), l_check);
        if (l_check) {
            const char *l_guuid_str = dap_uint128_to_hex_str(a_uuid.raw);
            log_it(L_ERROR, "GUUID %s already in use", l_guuid_str);
            DAP_DELETE(l_ret);
            return NULL;
        }
    }
    if (a_mnemonim) {
        l_ret->mnemonim = strdup(a_mnemonim);
        if (!l_ret->mnemonim) {
            log_it(L_CRITICAL, "Memory allocation error");
            DAP_DELETE(l_ret);
            return NULL;
        }
    }
    l_ret->uuid = a_uuid;
    HASH_ADD(hh, s_clusters, uuid, sizeof(l_ret->uuid), l_ret);
    pthread_rwlock_unlock(&s_clusters_rwlock);
    return l_ret;
}

dap_cluster_t *dap_cluster_find(dap_guuid_t a_uuid)
{
    dap_cluster_t *l_ret = NULL;
    pthread_rwlock_rdlock(&s_clusters_rwlock);
    HASH_FIND(hh, s_clusters, &a_uuid, sizeof(dap_guuid_t), l_ret);
    pthread_rwlock_unlock(&s_clusters_rwlock);
    return l_ret;
}

dap_cluster_t *dap_cluster_by_mnemonim(const char *a_mnemonim)
{
    dap_return_val_if_fail(a_mnemonim, NULL);
    dap_cluster_t *l_ret = NULL;
    pthread_rwlock_rdlock(&s_clusters_rwlock);
    HASH_FIND(hh_str, s_cluster_mnemonims, a_mnemonim, strlen(a_mnemonim), l_ret);
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
    if (a_cluster->mnemonim) {
        HASH_DELETE(hh_str, s_cluster_mnemonims, a_cluster);
        DAP_DELETE(a_cluster->mnemonim);
    }
    pthread_rwlock_unlock(&s_clusters_rwlock);
    dap_cluster_delete_all_members(a_cluster);
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
    dap_return_val_if_fail(a_cluster && a_addr, l_member);
    pthread_rwlock_wrlock(&a_cluster->members_lock);
    HASH_FIND(hh, a_cluster->members, a_addr, sizeof(*a_addr), l_member);
    if (l_member) {
        pthread_rwlock_unlock(&a_cluster->members_lock);
        log_it(L_WARNING, "Trying to add member "NODE_ADDR_FP_STR" but its already present in cluster ",
                                                NODE_ADDR_FP_ARGS(a_addr));
        return NULL;
    }
    l_member = DAP_NEW_Z(dap_cluster_member_t);
    if (!l_member) {
        log_it(L_CRITICAL, "%s", g_error_memory_alloc);
        pthread_rwlock_unlock(&a_cluster->members_lock);
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
        l_member->cluster->members_add_callback(l_member);
    return l_member;
}

/**
 * @brief dap_cluster_member_delete
 * @param a_member
 */
int dap_cluster_member_delete(dap_cluster_t *a_cluster, dap_stream_node_addr_t *a_member_addr)
{
    dap_return_val_if_fail(a_cluster && a_member_addr, -1);
    pthread_rwlock_wrlock(&a_cluster->members_lock);
    dap_cluster_member_t *l_member = NULL;
    HASH_FIND(hh, a_cluster->members, a_member_addr, sizeof(*a_member_addr), l_member);
    if (l_member)
        s_cluster_member_delete(l_member);
    pthread_rwlock_unlock(&a_cluster->members_lock);
    return l_member ? 0 : 1;
}

/**
 * @brief delete all members in cluster
 * @param a_cluster - cluster to clean
 */
void dap_cluster_delete_all_members(dap_cluster_t *a_cluster)
{
    dap_cluster_member_t *l_member, *l_tmp;
    pthread_rwlock_wrlock(&a_cluster->members_lock);
    HASH_ITER(hh, a_cluster->members, l_member, l_tmp)
        s_cluster_member_delete(l_member);
    pthread_rwlock_unlock(&a_cluster->members_lock);
}

static void s_cluster_member_delete(dap_cluster_member_t *a_member)
{
    if (a_member->cluster->members_delete_callback)
        a_member->cluster->members_delete_callback(a_member);
    HASH_DEL(a_member->cluster->members, a_member);
    DAP_DEL_Z(a_member->info);
    DAP_DELETE(a_member);
}

void dap_cluster_link_delete_from_all(dap_stream_node_addr_t *a_addr)
{
    pthread_rwlock_rdlock(&s_clusters_rwlock);
    for (dap_cluster_t *it = s_clusters; it; it = it->hh.next)
        if (it->role == DAP_CLUSTER_ROLE_AUTONOMIC ||
                it->role == DAP_CLUSTER_ROLE_EMBEDDED)
            dap_cluster_member_delete(it, a_addr);
    pthread_rwlock_unlock(&s_clusters_rwlock);
}

/**
 * @brief dap_cluster_member_find
 * @param a_cluster
 * @param a_member_id
 * @return
 */
dap_cluster_member_t *dap_cluster_member_find_unsafe(dap_cluster_t *a_cluster, dap_stream_node_addr_t *a_member_addr)
{
    dap_return_val_if_fail(a_cluster && a_member_addr, NULL);
    dap_cluster_member_t *l_member = NULL;
    pthread_rwlock_rdlock(&a_cluster->members_lock);
    HASH_FIND(hh, a_cluster->members, a_member_addr, sizeof(*a_member_addr), l_member);
    pthread_rwlock_unlock(&a_cluster->members_lock);
    return l_member;
}

int dap_cluster_member_find_role(dap_cluster_t *a_cluster, dap_stream_node_addr_t *a_member_addr)
{
    dap_return_val_if_fail(a_cluster && a_member_addr, -1);
    dap_cluster_member_t *l_member = NULL;
    pthread_rwlock_rdlock(&a_cluster->members_lock);
    HASH_FIND(hh, a_cluster->members, a_member_addr, sizeof(*a_member_addr), l_member);
    pthread_rwlock_unlock(&a_cluster->members_lock);
    return l_member ? l_member->role : -1;
}

bool s_present_in_array(dap_stream_node_addr_t a_addr, dap_stream_node_addr_t *a_array, size_t a_array_size)
{
    for (size_t i = 0; i < a_array_size; i++)
        if ((a_array + i)->uint64 == a_addr.uint64)
            return true;
    return false;
}

void dap_cluster_broadcast(dap_cluster_t *a_cluster, const char a_ch_id, uint8_t a_type, const void *a_data, size_t a_data_size,
                           dap_stream_node_addr_t *a_exclude_aray, size_t a_exclude_array_size)
{
    if (!a_cluster) {
        // TODO add exclude array to stream broadcasting
        dap_stream_broadcast(a_ch_id, a_type, a_data, a_data_size);
        return;
    }
    pthread_rwlock_rdlock(&a_cluster->members_lock);
    for (dap_cluster_member_t *it = a_cluster->members; it; it = it->hh.next) {
        if (s_present_in_array(it->addr, a_exclude_aray, a_exclude_array_size))
            continue;
        dap_stream_ch_pkt_send_by_addr(&it->addr, a_ch_id, a_type, a_data, a_data_size);
    }
    pthread_rwlock_unlock(&a_cluster->members_lock);
}

// Returns information about cluster links. For NULL cluster returns information about all node links
json_object *dap_cluster_get_links_info_json(dap_cluster_t *a_cluster){
    json_object *l_jobj_ret = json_object_new_object();
    json_object *l_jobj_downlinks = json_object_new_array();
    json_object *l_jobj_uplinks = json_object_new_array();
    if (!l_jobj_ret || !l_jobj_downlinks || !l_jobj_uplinks){
        json_object_put(l_jobj_ret);
        json_object_put(l_jobj_uplinks);
        json_object_put(l_jobj_downlinks);
        return NULL;
    }
    size_t l_total_links_count = 0;
    dap_stream_info_t *l_links_info = dap_stream_get_links_info(a_cluster, &l_total_links_count);
    if (l_links_info) {
        for (size_t i = 0; i < l_total_links_count; i++) {
            dap_stream_info_t *l_link_info = l_links_info + i;
            json_object *l_jobj_info = json_object_new_object();
            char *l_addr = dap_strdup_printf(NODE_ADDR_FP_STR, NODE_ADDR_FP_ARGS_S(l_links_info->node_addr));
            json_object *l_jobj_node_addr = json_object_new_string(l_addr);
            DAP_DELETE(l_addr);
            json_object *l_jobj_ip = json_object_new_string(l_links_info->remote_addr_str);
            json_object *l_jobj_port = json_object_new_int(l_links_info->remote_port);
            json_object *l_jobj_channel = json_object_new_string(l_links_info->channels);
            json_object *l_jobj_total_packets_sent  = json_object_new_uint64(l_links_info->total_packets_sent);
            if (!l_jobj_info || !l_jobj_node_addr || !l_jobj_ip || !l_jobj_port || !l_jobj_channel || !l_jobj_total_packets_sent) {
                json_object_put(l_jobj_info);
                json_object_put(l_jobj_node_addr);
                json_object_put(l_jobj_ip);
                json_object_put(l_jobj_port);
                json_object_put(l_jobj_channel);
                json_object_put(l_jobj_total_packets_sent);
                json_object_put(l_jobj_ret);
                json_object_put(l_jobj_downlinks);
                json_object_put(l_jobj_uplinks);
                return NULL;
            }
            json_object_object_add(l_jobj_info, "addr", l_jobj_node_addr);
            json_object_object_add(l_jobj_info, "ip", l_jobj_ip);
            json_object_object_add(l_jobj_info, "port", l_jobj_port);
            json_object_object_add(l_jobj_info, "channel", l_jobj_channel);
            json_object_object_add(l_jobj_info, "total_packets_sent", l_jobj_total_packets_sent);
            if (l_links_info->is_uplink) {
                json_object_array_add(l_jobj_uplinks, l_jobj_info);
            } else {
                json_object_array_add(l_jobj_downlinks, l_jobj_downlinks);
            }
        }
        dap_stream_delete_links_info(l_links_info, l_total_links_count);
    }
    assert(l_total_links_count == json_object_array_length(l_jobj_uplinks) + json_object_array_length(l_jobj_downlinks));
    if (json_object_array_length(l_jobj_uplinks)) {
        json_object_object_add(l_jobj_ret, "uplinks", l_jobj_uplinks);
    } else {
        json_object_object_add(l_jobj_ret, "uplinks", json_object_new_null());
    }
    if (json_object_array_length(l_jobj_downlinks)) {
        json_object_object_add(l_jobj_ret, "downlinks", l_jobj_downlinks);
    } else {
        json_object_object_add(l_jobj_ret, "downlinks", l_jobj_downlinks);
    }
    return l_jobj_ret;
}

char *dap_cluster_get_links_info(dap_cluster_t *a_cluster)
{
    dap_string_t *l_str_out = dap_string_new(" ↑\\↓ |\t\tNode addr\t| \tIP\t  |    Port\t|    Channels  | SeqID\n"
                                             "--------------------------------------------------------------------------------------\n");
    size_t l_uplinks_count = 0, l_downlinks_count = 0, l_total_links_count = 0;
    dap_stream_info_t *l_links_info = dap_stream_get_links_info(a_cluster, &l_total_links_count);
    if (l_links_info) {
        for (size_t i = 0; i < l_total_links_count; i++) {
            dap_stream_info_t *l_link_info = l_links_info + i;
            dap_string_append_printf(l_str_out, "  %s  | "NODE_ADDR_FP_STR"\t| %s |    %hu\t|\t%s\t| %zu\n",
                                     l_link_info->is_uplink ? "↑" : "↓",
                                     NODE_ADDR_FP_ARGS_S(l_link_info->node_addr),
                                     l_link_info->remote_addr_str,
                                     l_link_info->remote_port,
                                     l_link_info->channels,
                                     l_link_info->total_packets_sent
                                     );
            if (l_link_info->is_uplink)
                l_uplinks_count++;
            else
                l_downlinks_count++;
        }
        dap_stream_delete_links_info(l_links_info, l_total_links_count);
    }
    assert(l_total_links_count == l_uplinks_count + l_downlinks_count);
    dap_string_append_printf(l_str_out, "--------------------------------------------------------------------------------------\n"
                                        "Total links: %zu | Uplinks: %zu | Downlinks: %zu\n",
                                l_total_links_count, l_uplinks_count, l_downlinks_count);
    char *l_ret = l_str_out->str;
    dap_string_free(l_str_out, false);
    return l_ret;
}

dap_stream_node_addr_t dap_cluster_get_random_link(dap_cluster_t *a_cluster)
{
    dap_stream_node_addr_t l_ret = {};
    dap_return_val_if_fail(a_cluster, l_ret);
    if (a_cluster->members) {
        int num = rand() % HASH_COUNT(a_cluster->members), idx = 0;
        pthread_rwlock_rdlock(&a_cluster->members_lock);
        for (dap_cluster_member_t *it = a_cluster->members; it; it = it->hh.next) {
            if (idx++ == num) {
                l_ret = it->addr;
                break;
            }
        }
        pthread_rwlock_unlock(&a_cluster->members_lock);
    }
    return l_ret;
}

/**
 * @brief forming list with all members node addrs in cluster
 * @param a_count count of finded links
 * @return pointer to dap_stream_node_addr_t array
 */
dap_stream_node_addr_t *dap_cluster_get_all_members_addrs(dap_cluster_t *a_cluster, size_t *a_count)
{
// sanity check
    dap_return_val_if_pass(!a_cluster, NULL);
// func work
    size_t l_count = 0;
    dap_stream_node_addr_t *l_ret = NULL;

    pthread_rwlock_rdlock(&a_cluster->members_lock);
        if (a_cluster->members) {
            l_count = HASH_COUNT(a_cluster->members);
            l_ret = DAP_NEW_Z_COUNT(dap_stream_node_addr_t, l_count);
            if (!l_ret) {
                log_it(L_CRITICAL, "%s", g_error_memory_alloc);
                pthread_rwlock_unlock(&a_cluster->members_lock);
                return NULL;
            }
            dap_cluster_member_t *l_member = NULL, *l_tmp = NULL;
            size_t l_bias = 0;
            HASH_ITER(hh, a_cluster->members, l_member, l_tmp) {
                l_ret[l_bias].uint64 = l_member->addr.uint64;
                l_bias++;
            }
        }
    pthread_rwlock_unlock(&a_cluster->members_lock);
    if (a_count)
        a_count = l_count;
    return l_ret;
}
