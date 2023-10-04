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

#include "dap_global_db_cluster.h"
#include "dap_global_db_pkt.h"
#include "dap_strfuncs.h"
#include "dap_sign.h"
#include "dap_proc_thread.h"
#include "crc32c_adler/crc32c_adler.h"

int dap_global_db_cluster_init()
{

}

void dap_global_db_cluster_deinit()
{

}

dap_global_db_cluster_t *dap_global_db_cluster_by_group(dap_global_db_instance_t *a_dbi, const char *a_group_name)
{
    dap_global_db_cluster *it;
    DL_FOREACH(a_dbi->clusters, it)
        if (!dap_fnmatch(it->group_mask, a_group_name, 0))
            return it;
}

DAP_STATIC_INLINE s_object_is_new(dap_store_obj_t *a_store_obj)
{
    dap_nanotime_t l_time_diff = a_store_obj->timestamp - dap_nanotime_now();
    return l_time_diff < DAP_GLOBAL_DB_CLUSTER_BROADCAST_LIFETIME * 1000000000UL;
}

void dap_global_db_cluster_broadcast(dap_global_db_cluster_t *a_cluster, dap_store_obj_t *a_store_obj)
{
    if (!s_object_is_new(a_store_obj))
        return;         // Send new rumors only
    dap_global_db_pkt_t *l_pkt = dap_global_db_pkt_serialize(a_store_obj);
    dap_cluster_broadcast(dap_strcmp(a_cluster->mnemonim, DAP_GLOBAL_DB_CLUSTER_ANY) ? a_cluster->member_cluster : NULL,
                          DAP_STREAM_CH_GDB_ID, DAP_STREAM_CH_GDB_PKT_TYPE_GOSSIP, l_pkt, dap_global_db_pkt_get_size(l_pkt));
    DAP_DELETE(l_pkt);
}

dap_global_db_cluster_t *dap_global_db_cluster_add(dap_global_db_instance_t *a_dbi, const char *a_mnemonim,
                                                   const char *a_group_mask, uint64_t a_ttl, bool a_owner_access,
                                                   dap_store_obj_callback_notify_t a_callback, void *a_callback_arg,
                                                   dap_global_db_role_t a_default_role)
{
    if (!a_callback) {
        log_it(L_ERROR, "Trying to set NULL callback for mask %s", a_group_mask);
        return NULL;
    }
    dap_global_db_cluster_t *it;
    DL_FOREACH(a_dbi->clusters, it) {
        if (!dap_strcmp(it->group_mask, a_group_mask)) {
            log_it(L_WARNING, "Group mask '%s' already present in the list, ignore it", a_group_mask);
            return NULL;
        }
    }
    dap_global_db_cluster_t *l_cluster = DAP_NEW_Z(dap_global_db_cluster_t);
    if (!l_cluster) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    l_cluster->member_cluster = dap_cluster_new(0);
    if (!l_cluster->member_cluster) {
        log_it(L_ERROR, "Can't create member cluster");
        DAP_DELETE(l_cluster);
        return NULL;
    }
    l_cluster->group_mask = dap_strdup(a_group_mask);
    if (!l_cluster->group_mask) {
        log_it(L_CRITICAL, "Memory allocation error");
        dap_cluster_delete(l_cluster->member_cluster);
        DAP_DELETE(l_cluster);
        return NULL;
    }
    if (a_net_name) {
        l_cluster->mnemonim = dap_strdup(a_mnemonim);
        if (!l_cluster->mnemonim) {
            log_it(L_CRITICAL, "Memory allocation error");
            dap_cluster_delete(l_cluster->member_cluster);
            DAP_DELETE(l_cluster->groups_mask);
            DAP_DELETE(l_cluster);
            return NULL;
        }
    }
    l_cluster->callback_notify = a_callback;
    l_cluster->callback_arg = a_callback_arg;
    l_cluster->ttl = a_ttl;
    l_cluster->default_role = a_default_role;
    l_cluster->owner_root_access = a_owner_access;
    l_cluster->dbi = a_dbi;
    DL_APPEND(a_dbi->clusters, l_cluster);
    return l_cluster;
}

void dap_global_db_cluster_delete(dap_global_db_cluster_t *a_cluster)
{
    dap_cluster_delete(a_cluster->member_cluster);
    DAP_DEL_Z(a_cluster->mnemonim);
    DAP_DEL_Z(a_cluster->groups_mask);
    DL_DELETE(a_cluster->dbi->clusters, a_cluster);
    DAP_DELETE(a_cluster);
}

int dap_global_db_cluster_member_add(dap_global_db_instance_t *a_dbi, const char *a_mnemonim,
                                     dap_stream_node_addr_t a_node_addr, dap_global_db_role_t a_role)
{
    dap_global_db_cluster *it;
    int l_clusters_added = 0;
    DL_FOREACH(a_dbi->clusters, it)
        if (!dap_strcmp(it->mnemonim, a_mnemonim))
            l_cluster_added += dap_cluster_member_add(it->member_cluster, a_node_addr, a_role, NULL) ? 0 : 1;
    return l_clusters_added;
}

struct object_extender {
    dap_global_db_instance_t *dbi;
    dap_global_db_cluster_t *cluster;
};

static bool s_db_cluster_notify_on_proc_thread(dap_proc_thread_t UNUSED_ARG *a_thread, void *a_arg)
{
    dap_store_obj_t *l_store_obj = a_arg;
    struct object_extender *l_ext = (struct object_extender *)l_store_obj->ext;
    dap_global_db_cluster_t *l_db_cluser = l_ext->cluster;
    l_db_cluser->callback_notify(l_ext->dbi, l_store_obj, l_db_cluster->callback_arg);
    dap_store_obj_free_one(l_store_obj);
    return false;
}

void dap_global_db_cluster_notify(dap_global_db_cluster_t *a_cluster, dap_store_obj_t *a_store_obj)
{
    struct object_extender l_ext = { .dbi = a_dbi, .cluster = a_cluster };
    dap_store_obj_t *l_store_obj = dap_store_obj_copy_ext(a_store_obj, l_ext, sizeof(struct object_extender));
    dap_proc_thread_callback_add_pri(NULL, s_db_cluster_notify_on_proc_thread, l_store_obj, DAP_QUEUE_MSG_PRIORITY_LOW);
}
