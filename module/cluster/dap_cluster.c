/*
 * Authors:
 * Roman Khlopkov <roman.khlopkov@demlabs.net>
 * Cellframe       https://cellframe.net
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * DAP Cluster — node grouping, membership and broadcast.
 */

#include "dap_cluster.h"
#include "dap_list.h"
#include "dap_ht.h"
#include "dap_strfuncs.h"
#include "dap_string.h"
#include "dap_stream.h"
#include "dap_stream_ch_pkt.h"

#define LOG_TAG "dap_cluster"

static dap_cluster_t *s_clusters = NULL, *s_cluster_mnemonims = NULL;
static pthread_rwlock_t s_clusters_rwlock = PTHREAD_RWLOCK_INITIALIZER;

static dap_cluster_callbacks_t s_cluster_callbacks[DAP_CLUSTER_TYPE_VIRTUAL + 1] = {};

static dap_cluster_t *s_global_links_cluster = NULL;

static void s_cluster_member_delete(dap_cluster_member_t *a_member);
static void s_on_stream_member_add(dap_cluster_node_addr_t *a_addr);
static void s_on_stream_member_delete(dap_cluster_node_addr_t *a_addr);

int dap_cluster_callbacks_register(dap_cluster_type_t a_type,
                                    dap_cluster_member_add_callback_t a_add_cb,
                                    dap_cluster_member_delete_callback_t a_del_cb,
                                    void *a_arg)
{
    if (a_type <= DAP_CLUSTER_TYPE_INVALID || a_type > DAP_CLUSTER_TYPE_VIRTUAL)
        return -1;
    s_cluster_callbacks[a_type] = (dap_cluster_callbacks_t) {
        .add_callback = a_add_cb,
        .delete_callback = a_del_cb,
        .arg = a_arg
    };
    return 0;
}

dap_cluster_callbacks_t *dap_cluster_callbacks_get(dap_cluster_type_t a_type)
{
    if (a_type <= DAP_CLUSTER_TYPE_INVALID || a_type > DAP_CLUSTER_TYPE_VIRTUAL)
        return NULL;
    return &s_cluster_callbacks[a_type];
}

static void s_on_stream_member_add(dap_cluster_node_addr_t *a_addr)
{
    if (s_global_links_cluster)
        dap_cluster_member_add(s_global_links_cluster, a_addr, 0, NULL);
}

static void s_on_stream_member_delete(dap_cluster_node_addr_t *a_addr)
{
    if (s_global_links_cluster)
        dap_cluster_member_delete(s_global_links_cluster, a_addr);
}

int dap_cluster_init(void)
{
    s_global_links_cluster = dap_cluster_new(DAP_CLUSTER_GLOBAL,
                                             *(dap_guuid_t *)&uint128_0,
                                             DAP_CLUSTER_TYPE_SYSTEM);
    if (!s_global_links_cluster) {
        log_it(L_CRITICAL, "Can't create global links cluster");
        return -1;
    }
    dap_stream_set_member_callbacks(s_on_stream_member_add,
                                    s_on_stream_member_delete);
    return 0;
}

dap_cluster_t *dap_cluster_new(const char *a_mnemonim, dap_guuid_t a_guuid, dap_cluster_type_t a_type)
{
    dap_cluster_t *ret = DAP_NEW_Z(dap_cluster_t);
    if (!ret) {
        log_it(L_CRITICAL, "Insufficient memory");
        return NULL;
    }
    pthread_rwlock_init(&ret->members_lock, NULL);
    ret->type = a_type;
    ret->guuid = a_guuid;
    if (a_type == DAP_CLUSTER_TYPE_VIRTUAL)
        return ret;
    dap_cluster_t *l_check = NULL;
    pthread_rwlock_wrlock(&s_clusters_rwlock);
    dap_ht_find(s_clusters, &a_guuid, sizeof(dap_guuid_t), l_check);
    if (l_check) {
        log_it(L_ERROR, "GUUID %s already in use", dap_guuid_to_hex_str(a_guuid));
        DAP_DELETE(ret);
        return NULL;
    }
    if (a_mnemonim) {
        dap_ht_find_str_hh(hh_str, s_cluster_mnemonims, a_mnemonim, l_check);
        if (l_check) {
            log_it(L_ERROR, "Mnemonim %s already in use", a_mnemonim);
            DAP_DELETE(ret);
            return NULL;
        }
        ret->mnemonim = strdup(a_mnemonim);
        if (!ret->mnemonim) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            DAP_DELETE(ret);
            return NULL;
        }
        dap_ht_add_keyptr_hh(hh_str, s_cluster_mnemonims, a_mnemonim, strlen(a_mnemonim), ret);
    }
    dap_ht_add(s_clusters, guuid, ret);
    pthread_rwlock_unlock(&s_clusters_rwlock);
    return ret;
}

dap_cluster_t *dap_cluster_find(dap_guuid_t a_uuid)
{
    dap_cluster_t *ret = NULL;
    pthread_rwlock_rdlock(&s_clusters_rwlock);
    dap_ht_find(s_clusters, &a_uuid, sizeof(dap_guuid_t), ret);
    pthread_rwlock_unlock(&s_clusters_rwlock);
    return ret;
}

dap_cluster_t *dap_cluster_by_mnemonim(const char *a_mnemonim)
{
    dap_return_val_if_fail(a_mnemonim, NULL);
    dap_cluster_t *ret = NULL;
    pthread_rwlock_rdlock(&s_clusters_rwlock);
    dap_ht_find_str_hh(hh_str, s_cluster_mnemonims, a_mnemonim, ret);
    pthread_rwlock_unlock(&s_clusters_rwlock);
    return ret;
}

void dap_cluster_delete(dap_cluster_t *a_cluster)
{
    if (!a_cluster)
        return;
    pthread_rwlock_wrlock(&s_clusters_rwlock);
    dap_ht_del(s_clusters, a_cluster);
    if (a_cluster->mnemonim) {
        dap_ht_del_hh(hh_str, s_cluster_mnemonims, a_cluster);
        DAP_DELETE(a_cluster->mnemonim);
    }
    pthread_rwlock_unlock(&s_clusters_rwlock);
    dap_cluster_delete_all_members(a_cluster);
    assert(!a_cluster->_inheritor);
    DAP_DELETE(a_cluster);
}

dap_cluster_member_t *dap_cluster_member_add(dap_cluster_t *a_cluster, dap_cluster_node_addr_t *a_addr, int a_role, void *a_info)
{
    dap_cluster_member_t *l_member = NULL;
    dap_return_val_if_fail(a_cluster && a_addr, l_member);
    pthread_rwlock_wrlock(&a_cluster->members_lock);
    dap_ht_find(a_cluster->members, a_addr, sizeof(*a_addr), l_member);
    if (l_member) {
        pthread_rwlock_unlock(&a_cluster->members_lock);
        log_it(L_DEBUG, "Member " NODE_ADDR_FP_STR " already present in cluster",
               NODE_ADDR_FP_ARGS(a_addr));
        return l_member;
    }
    l_member = DAP_NEW_Z(dap_cluster_member_t);
    if (!l_member) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        pthread_rwlock_unlock(&a_cluster->members_lock);
        return NULL;
    }
    *l_member = (dap_cluster_member_t) {
        .addr       = *a_addr,
        .cluster    = a_cluster,
        .role       = a_role,
        .info       = a_info
    };
    dap_ht_add(a_cluster->members, addr, l_member);
    pthread_rwlock_unlock(&a_cluster->members_lock);
    if (a_cluster->members_add_callback)
        a_cluster->members_add_callback(l_member, a_cluster->callbacks_arg);
    return l_member;
}

void dap_cluster_members_register(dap_cluster_t *a_cluster)
{
    dap_cluster_member_t *l_member = NULL, *l_member_tmp = NULL;
    pthread_rwlock_rdlock(&a_cluster->members_lock);
    dap_ht_foreach(a_cluster->members, l_member, l_member_tmp)
        if (a_cluster->members_add_callback)
            a_cluster->members_add_callback(l_member, a_cluster->callbacks_arg);
    pthread_rwlock_unlock(&a_cluster->members_lock);
}

int dap_cluster_member_delete(dap_cluster_t *a_cluster, dap_cluster_node_addr_t *a_member_addr)
{
    dap_return_val_if_fail(a_cluster && a_member_addr, -1);
    pthread_rwlock_wrlock(&a_cluster->members_lock);
    dap_cluster_member_t *l_member = NULL;
    dap_ht_find(a_cluster->members, a_member_addr, sizeof(*a_member_addr), l_member);
    if (l_member)
        s_cluster_member_delete(l_member);
    pthread_rwlock_unlock(&a_cluster->members_lock);
    return l_member ? 0 : 1;
}

void dap_cluster_delete_all_members(dap_cluster_t *a_cluster)
{
    dap_cluster_member_t *l_member, *l_tmp;
    pthread_rwlock_wrlock(&a_cluster->members_lock);
    dap_ht_foreach(a_cluster->members, l_member, l_tmp)
        s_cluster_member_delete(l_member);
    pthread_rwlock_unlock(&a_cluster->members_lock);
}

static void s_cluster_member_delete(dap_cluster_member_t *a_member)
{
    if (a_member->cluster->members_delete_callback)
        a_member->cluster->members_delete_callback(a_member, a_member->cluster->callbacks_arg);
    dap_ht_del(a_member->cluster->members, a_member);
    DAP_DEL_Z(a_member->info);
    DAP_DELETE(a_member);
}

void dap_cluster_link_delete_from_all(dap_list_t *a_cluster_list, dap_cluster_node_addr_t *a_addr)
{
    pthread_rwlock_rdlock(&s_clusters_rwlock);
    dap_list_t *l_list = dap_list_copy(a_cluster_list);
    for (dap_list_t *it = l_list; it; it = it->next) {
        dap_cluster_t *l_cluster = it->data;
        dap_cluster_member_delete(l_cluster, a_addr);
    }
    dap_list_free(l_list);
    pthread_rwlock_unlock(&s_clusters_rwlock);
}

dap_cluster_member_t *dap_cluster_member_find_unsafe(dap_cluster_t *a_cluster, dap_cluster_node_addr_t *a_member_addr)
{
    dap_return_val_if_fail(a_cluster && a_member_addr, NULL);
    dap_cluster_member_t *l_member = NULL;
    pthread_rwlock_rdlock(&a_cluster->members_lock);
    dap_ht_find(a_cluster->members, a_member_addr, sizeof(*a_member_addr), l_member);
    pthread_rwlock_unlock(&a_cluster->members_lock);
    return l_member;
}

int dap_cluster_member_find_role(dap_cluster_t *a_cluster, dap_cluster_node_addr_t *a_member_addr)
{
    dap_return_val_if_fail(a_cluster && a_member_addr, -1);
    dap_cluster_member_t *l_member = NULL;
    pthread_rwlock_rdlock(&a_cluster->members_lock);
    dap_ht_find(a_cluster->members, a_member_addr, sizeof(*a_member_addr), l_member);
    pthread_rwlock_unlock(&a_cluster->members_lock);
    return l_member ? l_member->role : -1;
}

static bool s_present_in_array(dap_cluster_node_addr_t a_addr, dap_cluster_node_addr_t *a_array, size_t a_array_size)
{
    for (size_t i = 0; i < a_array_size; i++)
        if ((a_array + i)->uint64 == a_addr.uint64)
            return true;
    return false;
}

void dap_cluster_broadcast(dap_cluster_t *a_cluster, const char a_ch_id, uint8_t a_type, const void *a_data, size_t a_data_size,
                           dap_cluster_node_addr_t *a_exclude_aray, size_t a_exclude_array_size)
{
    dap_return_if_fail(a_cluster);
    pthread_rwlock_rdlock(&a_cluster->members_lock);
    for (dap_cluster_member_t *it = a_cluster->members; it; it = it->hh.next) {
        if (s_present_in_array(it->addr, a_exclude_aray, a_exclude_array_size))
            continue;
        dap_stream_ch_pkt_send_by_addr(&it->addr, a_ch_id, a_type, a_data, a_data_size);
    }
    pthread_rwlock_unlock(&a_cluster->members_lock);
}

dap_json_t *dap_cluster_get_links_info_json(dap_cluster_t *a_cluster)
{
    dap_json_t *l_jobj_ret = dap_json_object_new();
    dap_json_t *l_jobj_downlinks = dap_json_array_new();
    dap_json_t *l_jobj_uplinks = dap_json_array_new();
    if (!l_jobj_ret || !l_jobj_downlinks || !l_jobj_uplinks) {
        dap_json_object_free(l_jobj_ret);
        dap_json_object_free(l_jobj_uplinks);
        dap_json_object_free(l_jobj_downlinks);
        return NULL;
    }
    size_t l_total_links_count = 0;
    dap_stream_info_t *l_links_info = NULL;
    if (a_cluster) {
        size_t l_addrs_count = 0;
        dap_cluster_node_addr_t *l_addrs = dap_cluster_get_all_members_addrs(a_cluster, &l_addrs_count, -1);
        if (l_addrs && l_addrs_count)
            l_links_info = dap_stream_get_links_info_by_addrs(l_addrs, l_addrs_count, &l_total_links_count);
        DAP_DEL_Z(l_addrs);
    } else {
        l_links_info = dap_stream_get_all_links_info(&l_total_links_count);
    }
    if (l_links_info) {
        for (size_t i = 0; i < l_total_links_count; i++) {
            dap_stream_info_t *l_link_info = l_links_info + i;
            dap_json_t *l_jobj_info = dap_json_object_new();
            if (!l_jobj_info) {
                dap_json_object_free(l_jobj_ret);
                dap_json_object_free(l_jobj_downlinks);
                dap_json_object_free(l_jobj_uplinks);
                dap_stream_delete_links_info(l_links_info, l_total_links_count);
                return NULL;
            }
            dap_json_object_add_string(l_jobj_info, "addr", dap_cluster_node_addr_to_str(l_link_info->node_addr));
            dap_json_object_add_string(l_jobj_info, "ip", l_link_info->remote_addr_str);
            dap_json_object_add_int(l_jobj_info, "port", l_link_info->remote_port);
            dap_json_object_add_string(l_jobj_info, "channel", l_link_info->channels);
            dap_json_object_add_uint64(l_jobj_info, "total_packets_sent", l_link_info->total_packets_sent);
            dap_json_array_add(l_link_info->is_uplink ? l_jobj_uplinks : l_jobj_downlinks, l_jobj_info);
        }
        dap_stream_delete_links_info(l_links_info, l_total_links_count);
    }
    assert(l_total_links_count == dap_json_array_length(l_jobj_uplinks) + dap_json_array_length(l_jobj_downlinks));
    if (dap_json_array_length(l_jobj_uplinks)) {
        dap_json_object_add_array(l_jobj_ret, "uplinks", l_jobj_uplinks);
    } else {
        dap_json_object_free(l_jobj_uplinks);
        dap_json_object_add_null(l_jobj_ret, "uplinks");
    }
    if (dap_json_array_length(l_jobj_downlinks)) {
        dap_json_object_add_array(l_jobj_ret, "downlinks", l_jobj_downlinks);
    } else {
        dap_json_object_free(l_jobj_downlinks);
        dap_json_object_add_null(l_jobj_ret, "downlinks");
    }
    return l_jobj_ret;
}

char *dap_cluster_get_links_info(dap_cluster_t *a_cluster)
{
    dap_string_t *l_str_out = dap_string_new("");
    dap_string_append_printf(l_str_out, "Link inforamtion for cluster GUUID %s \n",
                                    a_cluster ? dap_guuid_to_hex_str(a_cluster->guuid) : "0 (global)");
    dap_string_append(l_str_out, " ↑\\↓ |\t\tNode addr\t| \tIP\t  |    Port\t|    Channels  | SeqID\n"
                                 "--------------------------------------------------------------------------------------\n");
    size_t l_uplinks_count = 0, l_downlinks_count = 0, l_total_links_count = 0;
    dap_stream_info_t *l_links_info = NULL;
    if (a_cluster) {
        size_t l_addrs_count = 0;
        dap_cluster_node_addr_t *l_addrs = dap_cluster_get_all_members_addrs(a_cluster, &l_addrs_count, -1);
        if (l_addrs && l_addrs_count)
            l_links_info = dap_stream_get_links_info_by_addrs(l_addrs, l_addrs_count, &l_total_links_count);
        DAP_DEL_Z(l_addrs);
    } else {
        l_links_info = dap_stream_get_all_links_info(&l_total_links_count);
    }
    if (l_links_info) {
        for (size_t i = 0; i < l_total_links_count; i++) {
            dap_stream_info_t *l_link_info = l_links_info + i;
            dap_string_append_printf(l_str_out, "  %s  | " NODE_ADDR_FP_STR "\t| %s |    %hu\t|\t%s\t| %zu\n",
                                     l_link_info->is_uplink ? "↑" : "↓",
                                     NODE_ADDR_FP_ARGS_S(l_link_info->node_addr),
                                     l_link_info->remote_addr_str,
                                     l_link_info->remote_port,
                                     l_link_info->channels,
                                     l_link_info->total_packets_sent);
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
    char *ret = l_str_out->str;
    dap_string_free(l_str_out, false);
    return ret;
}

dap_cluster_node_addr_t dap_cluster_get_random_link(dap_cluster_t *a_cluster)
{
    dap_cluster_node_addr_t ret = {};
    dap_return_val_if_fail(a_cluster, ret);
    if (a_cluster->members) {
        int num = rand() % (int)dap_ht_count(a_cluster->members), idx = 0;
        pthread_rwlock_rdlock(&a_cluster->members_lock);
        dap_cluster_member_t *it = NULL, *it_tmp = NULL;
        dap_ht_foreach(a_cluster->members, it, it_tmp) {
            if (idx++ == num) {
                ret = it->addr;
                break;
            }
        }
        pthread_rwlock_unlock(&a_cluster->members_lock);
    }
    return ret;
}

size_t dap_cluster_members_count(dap_cluster_t *a_cluster)
{
    dap_return_val_if_pass(!a_cluster, 0);
    pthread_rwlock_rdlock(&a_cluster->members_lock);
    size_t ret = dap_ht_count(a_cluster->members);
    pthread_rwlock_unlock(&a_cluster->members_lock);
    return ret;
}

dap_cluster_node_addr_t *dap_cluster_get_all_members_addrs(dap_cluster_t *a_cluster, size_t *a_count, int a_role)
{
    dap_return_val_if_pass(!a_cluster, NULL);
    size_t l_count = 0, l_bias = 0;
    dap_cluster_node_addr_t *ret = NULL;

    pthread_rwlock_rdlock(&a_cluster->members_lock);
    if (a_cluster->members) {
        l_count = dap_ht_count(a_cluster->members);
        ret = DAP_NEW_Z_COUNT(dap_cluster_node_addr_t, l_count);
        if (!ret) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            pthread_rwlock_unlock(&a_cluster->members_lock);
            return NULL;
        }
        dap_cluster_member_t *l_member = NULL, *l_member_tmp = NULL;
        dap_ht_foreach(a_cluster->members, l_member, l_member_tmp) {
            if (a_role == -1 || l_member->role == a_role) {
                ret[l_bias].uint64 = l_member->addr.uint64;
                l_bias++;
            }
        }
        if (l_bias < l_count)
            ret = DAP_REALLOC_COUNT(ret, l_bias);
    }
    pthread_rwlock_unlock(&a_cluster->members_lock);
    if (a_count)
        *a_count = l_bias;
    return ret;
}
