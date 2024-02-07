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

#include "dap_common.h"
#include "dap_global_db.h"
#include "dap_global_db_cluster.h"
#include "dap_global_db_pkt.h"
#include "dap_global_db_ch.h"
#include "dap_strfuncs.h"
#include "dap_sign.h"
#include "dap_proc_thread.h"
#include "dap_hash.h"
#include "dap_stream_ch_gossip.h"

#define LOG_TAG "dap_global_db_cluster"

int dap_global_db_cluster_init()
{
    dap_global_db_ch_init();
        // Pseudo-cluster for local scope (unsynced groups). There is no notifier for it
    if (dap_global_db_cluster_add(dap_global_db_instance_get_default(), DAP_GLOBAL_DB_CLUSTER_LOCAL, DAP_GLOBAL_DB_CLUSTER_LOCAL ".*",
                                    0, false, DAP_GDB_MEMBER_ROLE_ROOT, DAP_CLUSTER_ROLE_VIRTUAL))
        // Pseudo-cluster for global scope
        return !dap_global_db_cluster_add(dap_global_db_instance_get_default(), DAP_GLOBAL_DB_CLUSTER_GLOBAL, DAP_GLOBAL_DB_CLUSTER_GLOBAL ".*",
                                           DAP_GLOBAL_DB_UNCLUSTERED_TTL, true,
                                           DAP_GDB_MEMBER_ROLE_GUEST, DAP_CLUSTER_ROLE_EMBEDDED);
    return 2;
}

void dap_global_db_cluster_deinit()
{
    dap_global_db_instance_t *l_dbi = dap_global_db_instance_get_default();
    if (l_dbi) {
        dap_global_db_cluster_t *it, *tmp;
        DL_FOREACH_SAFE(l_dbi->clusters, it, tmp)
            dap_global_db_cluster_delete(it);
    }
}

dap_global_db_cluster_t *dap_global_db_cluster_by_group(dap_global_db_instance_t *a_dbi, const char *a_group_name)
{
    dap_global_db_cluster_t *it;
    DL_FOREACH(a_dbi->clusters, it)
        if (dap_global_db_group_match_mask(a_group_name, it->groups_mask))
            return it;
    return NULL;
}

void dap_global_db_cluster_broadcast(dap_global_db_cluster_t *a_cluster, dap_store_obj_t *a_store_obj)
{
    dap_global_db_pkt_t *l_pkt = dap_global_db_pkt_serialize(a_store_obj);
    union hash_convert {
        dap_hash_fast_t gossip_hash;
        dap_global_db_driver_hash_t gdb_hash;
    } l_hash_cvt = {};
    l_hash_cvt.gdb_hash = dap_global_db_driver_hash_get(a_store_obj);
    dap_gossip_msg_issue(a_cluster->links_cluster, DAP_STREAM_CH_GDB_ID, l_pkt, dap_global_db_pkt_get_size(l_pkt), &l_hash_cvt.gossip_hash);
    DAP_DELETE(l_pkt);
}

dap_global_db_cluster_t *dap_global_db_cluster_add(dap_global_db_instance_t *a_dbi, const char *a_mnemonim,
                                                   const char *a_group_mask, uint32_t a_ttl, bool a_owner_root_access,
                                                   dap_global_db_role_t a_default_role, dap_cluster_role_t a_links_cluster_role)
{
    dap_global_db_cluster_t *it;
    DL_FOREACH(a_dbi->clusters, it) {
        if (!dap_strcmp(it->groups_mask, a_group_mask)) {
            log_it(L_WARNING, "Group mask '%s' already present in the list, ignore it", a_group_mask);
            return NULL;
        }
    }
    dap_global_db_cluster_t *l_cluster = DAP_NEW_Z(dap_global_db_cluster_t);
    if (!l_cluster) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    l_cluster->links_cluster = dap_cluster_by_mnemonim(a_mnemonim);
    if (!l_cluster->links_cluster && dap_strcmp(DAP_GLOBAL_DB_CLUSTER_GLOBAL, a_mnemonim)) {
        l_cluster->links_cluster = dap_cluster_new(a_mnemonim, a_links_cluster_role);
        if (!l_cluster->links_cluster) {
            log_it(L_ERROR, "Can't create links cluster");
            DAP_DELETE(l_cluster);
            return NULL;
        }
    }
    if (dap_strcmp(DAP_GLOBAL_DB_CLUSTER_LOCAL, a_mnemonim)) {
    l_cluster->role_cluster = dap_cluster_new(NULL, DAP_CLUSTER_ROLE_VIRTUAL);
        if (!l_cluster->role_cluster) {
            log_it(L_ERROR, "Can't create role cluster");
            dap_cluster_delete(l_cluster->links_cluster);
            DAP_DELETE(l_cluster);
            return NULL;
        }
    }
    l_cluster->groups_mask = dap_strdup(a_group_mask);
    if (!l_cluster->groups_mask) {
        log_it(L_CRITICAL, "Memory allocation error");
        dap_cluster_delete(l_cluster->role_cluster);
        dap_cluster_delete(l_cluster->links_cluster);
        DAP_DELETE(l_cluster);
        return NULL;
    }
    l_cluster->ttl = (uint64_t)a_ttl * 3600;    // Convert to seconds
    l_cluster->default_role = a_default_role;
    l_cluster->owner_root_access = a_owner_root_access;
    l_cluster->dbi = a_dbi;
    l_cluster->link_manager = dap_link_manager_get_default();
    DL_APPEND(a_dbi->clusters, l_cluster);
    return l_cluster;
}

dap_cluster_member_t *dap_global_db_cluster_member_add(dap_global_db_cluster_t *a_cluster, dap_stream_node_addr_t *a_node_addr, dap_global_db_role_t a_role)
{
    if (!a_cluster || !a_node_addr) {
        log_it(L_ERROR, "Invalid argument with cluster member adding");
        return NULL;
    }
    // if (a_cluster->links_cluster &&
    //         (a_cluster->links_cluster->role == DAP_CLUSTER_ROLE_AUTONOMIC ||
    //          a_cluster->links_cluster->role == DAP_CLUSTER_ROLE_ISOLATED))
    //     dap_cluster_member_add(a_cluster->links_cluster, a_node_addr, a_role, NULL);

    return dap_cluster_member_add(a_cluster->role_cluster, a_node_addr, a_role, NULL);
}

void dap_global_db_cluster_delete(dap_global_db_cluster_t *a_cluster)
{
    if (a_cluster->links_cluster)
        dap_cluster_delete(a_cluster->links_cluster);
    if (a_cluster->role_cluster)
        dap_cluster_delete(a_cluster->role_cluster);
    if (a_cluster->link_manager && a_cluster->link_manager->callbacks.delete)
        a_cluster->link_manager->callbacks.delete(a_cluster->link_manager);
    
    DAP_DEL_MULTY(a_cluster->link_manager, a_cluster->groups_mask);
    DL_DELETE(a_cluster->dbi->clusters, a_cluster);
    DAP_DELETE(a_cluster);
}

static bool s_db_cluster_notify_on_proc_thread(dap_proc_thread_t UNUSED_ARG *a_thread, void *a_arg)
{
    dap_store_obj_t *l_store_obj = a_arg;
    dap_global_db_notifier_t *l_notifier = *(dap_global_db_notifier_t **)l_store_obj->ext;
    l_notifier->callback_notify(l_store_obj, l_notifier->callback_arg);
    dap_store_obj_free_one(l_store_obj);
    return false;
}

void dap_global_db_cluster_notify(dap_global_db_cluster_t *a_cluster, dap_store_obj_t *a_store_obj)
{
    dap_global_db_notifier_t *l_notifier;
    DL_FOREACH(a_cluster->notifiers, l_notifier) {
        assert(l_notifier->callback_notify);
        dap_store_obj_t *l_store_obj = dap_store_obj_copy_ext(a_store_obj, l_notifier, sizeof(*l_notifier));
        dap_proc_thread_callback_add_pri(NULL, s_db_cluster_notify_on_proc_thread, l_store_obj, DAP_QUEUE_MSG_PRIORITY_LOW);
    }
}

int dap_global_db_cluster_add_notify_callback(dap_global_db_cluster_t *a_cluster, dap_store_obj_callback_notify_t a_callback, void *a_callback_arg)
{
    dap_return_val_if_fail(a_cluster && a_callback, -1);
    dap_global_db_notifier_t *l_notifier = DAP_NEW_Z(dap_global_db_notifier_t);
    if (!l_notifier) {
        log_it(L_CRITICAL, "Not enough memory");
        return -2;
    }
    l_notifier->callback_notify = a_callback;
    l_notifier->callback_arg = a_callback_arg;
    DL_APPEND(a_cluster->notifiers, l_notifier);
    return 0;
}