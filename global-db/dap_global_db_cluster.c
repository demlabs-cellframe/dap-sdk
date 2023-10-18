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

static void s_callback_unclustered_notify(dap_global_db_instance_t *a_dbi, dap_store_obj_t *a_obj, void *a_arg)
{
    // TODO set and call global changes notificators
}

int dap_global_db_cluster_init()
{
    return !!dap_global_db_cluster_add(dap_global_db_instance_get_default(), DAP_GLOBAL_DB_CLUSTER_ANY, DAP_GLOBAL_DB_CLUSTER_ANY ".*",
                                       DAP_GLOBAL_DB_UNCLUSTERED_TTL, true, s_callback_unclustered_notify, NULL,
                                       DAP_GDB_MEMBER_ROLE_USER, DAP_CLUSTER_ROLE_EMBEDDED);
}

void dap_global_db_cluster_deinit()
{
    dap_global_db_cluster_t *it, *tmp;
    DL_FOREACH_SAFE(dap_global_db_instance_get_default()->clusters, it, tmp)
        dap_global_db_cluster_delete(it);
}

static bool s_group_match_mask(const char *a_group, const char *a_mask)
{
    dap_return_val_if_fail(a_group && a_mask && *a_group && *a_mask, false);
    const char *l_group_tail = a_group + strlen(a_group);           // Pointer to trailng zero
    if (!strcmp(l_group_tail - sizeof(DAP_GLOBAL_DB_DEL_SUFFIX), DAP_GLOBAL_DB_DEL_SUFFIX))
        l_group_tail -= sizeof(DAP_GLOBAL_DB_DEL_SUFFIX);           // Pointer to '.' of .del group suffix
    const char *l_mask_tail = a_mask + strlen(a_mask);
    const char *l_group_it = a_group, *l_mask_it = a_mask;
    const char *l_wildcard = strchr(a_mask, '*');
    while (l_mask_it < (l_wildcard ? l_wildcard : l_mask_tail) &&
                l_group_it < l_group_tail)
        if (l_group_it++ != l_mask_it++)
            return false;
    if (l_mask_it == l_wildcard && ++l_mask_it < l_mask_tail)
        return strstr(l_group_it, l_mask_it);
    return true;
}

dap_global_db_cluster_t *dap_global_db_cluster_by_group(dap_global_db_instance_t *a_dbi, const char *a_group_name)
{
    dap_global_db_cluster_t *it;
    DL_FOREACH(a_dbi->clusters, it)
        if (s_group_match_mask(a_group_name, it->groups_mask))
            return it;
    return NULL;
}

void dap_global_db_cluster_broadcast(dap_global_db_cluster_t *a_cluster, dap_store_obj_t *a_store_obj)
{
    dap_global_db_pkt_t *l_pkt = dap_global_db_pkt_serialize(a_store_obj);
    union hash_convert {
        dap_hash_fast_t gossip_hash;
        dap_global_db_hash_t gdb_hash;
    } l_hash_cvt = {};
    l_hash_cvt.gdb_hash.timestamp = a_store_obj->timestamp;
    l_hash_cvt.gdb_hash.crc = a_store_obj->crc;
    dap_gossip_msg_issue(a_cluster->links_cluster, DAP_STREAM_CH_GDB_ID, l_pkt, dap_global_db_pkt_get_size(l_pkt), &l_hash_cvt.gossip_hash);
    DAP_DELETE(l_pkt);
}

dap_global_db_cluster_t *dap_global_db_cluster_add(dap_global_db_instance_t *a_dbi, const char *a_mnemonim,
                                                   const char *a_group_mask, uint32_t a_ttl, bool a_owner_root_access,
                                                   dap_store_obj_callback_notify_t a_callback, void *a_callback_arg,
                                                   dap_global_db_role_t a_default_role, dap_cluster_role_t a_links_cluster_role)
{
    if (!a_callback) {
        log_it(L_ERROR, "Trying to set NULL callback for mask %s", a_group_mask);
        return NULL;
    }
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
    // TODO set NULL for 'global' mnemonim
    l_cluster->links_cluster = dap_cluster_by_mnemonim(a_mnemonim);
    if (!l_cluster->links_cluster) {
        l_cluster->links_cluster = dap_cluster_new(a_mnemonim, a_links_cluster_role);
        if (!l_cluster->links_cluster) {
            log_it(L_ERROR, "Can't create member cluster");
            DAP_DELETE(l_cluster);
            return NULL;
        }
    }
    l_cluster->groups_mask = dap_strdup(a_group_mask);
    if (!l_cluster->groups_mask) {
        log_it(L_CRITICAL, "Memory allocation error");
        dap_cluster_delete(l_cluster->links_cluster);
        DAP_DELETE(l_cluster);
        return NULL;
    }
    l_cluster->callback_notify = a_callback;
    l_cluster->callback_arg = a_callback_arg;
    l_cluster->ttl = (uint64_t)a_ttl * 3600;    // Convert to seconds
    l_cluster->default_role = a_default_role;
    l_cluster->owner_root_access = a_owner_root_access;
    l_cluster->dbi = a_dbi;
    DL_APPEND(a_dbi->clusters, l_cluster);
    return l_cluster;
}

void dap_global_db_cluster_delete(dap_global_db_cluster_t *a_cluster)
{
    dap_cluster_delete(a_cluster->links_cluster);
    DAP_DEL_Z(a_cluster->groups_mask);
    DL_DELETE(a_cluster->dbi->clusters, a_cluster);
    DAP_DELETE(a_cluster);
}

static bool s_db_cluster_notify_on_proc_thread(dap_proc_thread_t UNUSED_ARG *a_thread, void *a_arg)
{
    dap_store_obj_t *l_store_obj = a_arg;
    dap_global_db_cluster_t *l_db_cluster = *(dap_global_db_cluster_t **)l_store_obj->ext;
    l_db_cluster->callback_notify(l_db_cluster->dbi, l_store_obj, l_db_cluster->callback_arg);
    dap_store_obj_free_one(l_store_obj);
    return false;
}

void dap_global_db_cluster_notify(dap_global_db_cluster_t *a_cluster, dap_store_obj_t *a_store_obj)
{
    dap_store_obj_t *l_store_obj = dap_store_obj_copy_ext(a_store_obj, &a_cluster, sizeof(void *));
    dap_proc_thread_callback_add_pri(NULL, s_db_cluster_notify_on_proc_thread, l_store_obj, DAP_QUEUE_MSG_PRIORITY_LOW);
}
