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
#include "dap_strfuncs.h"
#include "dap_sign.h"
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
                                                   const char *a_group_mask, uint64_t a_ttl,
                                                   dap_store_obj_callback_notify_t a_callback, void *a_callback_arg)
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
    DL_APPEND(a_dbi->clusters, l_cluster);
    return l_cluster;
}
