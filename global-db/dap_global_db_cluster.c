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
#include "dap_proc_thread.h"
#include "dap_hash.h"
#include "dap_stream_ch_gossip.h"

#define LOG_TAG "dap_global_db_cluster"

static void s_gdb_cluster_sync_timer_callback(void *a_arg);

static dap_global_db_cluster_t *s_local_cluster = NULL, *s_global_cluster = NULL;

int dap_global_db_cluster_init()
{
    dap_global_db_ch_init();
    // Pseudo-cluster for global scope
    if ( !(s_global_cluster = dap_global_db_cluster_add(
                dap_global_db_instance_get_default(), DAP_STREAM_CLUSTER_GLOBAL,
                *(dap_guuid_t*)&uint128_0, DAP_GLOBAL_DB_CLUSTER_GLOBAL,
                dap_config_get_item_uint64_default(g_config, "global_db", "ttl_unclustered", DAP_GLOBAL_DB_UNCLUSTERED_TTL),
                true, DAP_GDB_MEMBER_ROLE_GUEST, DAP_CLUSTER_TYPE_SYSTEM)))
        return -1;

    // Pseudo-cluster for local scope (unsynced groups).
    if ( !(s_local_cluster = dap_global_db_cluster_add(
                dap_global_db_instance_get_default(), DAP_STREAM_CLUSTER_LOCAL,
                dap_guuid_compose(0, 1), DAP_GLOBAL_DB_CLUSTER_LOCAL,
                0, false, DAP_GDB_MEMBER_ROLE_NOBODY, DAP_CLUSTER_TYPE_SYSTEM)))
        return -2;

    dap_global_db_cluster_member_add(s_local_cluster, &g_node_addr, DAP_GDB_MEMBER_ROLE_ROOT);

    return 0;
}

void dap_global_db_cluster_deinit()
{
    // Reset static cluster pointers to avoid accessing freed memory
    s_local_cluster = NULL;
    s_global_cluster = NULL;
    
    // NOTE: Cluster cleanup is handled by instance_deinit
    // Calling dap_global_db_cluster_delete here causes double-free
    // because instance structures may already be partially freed
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

dap_global_db_cluster_t *dap_global_db_cluster_add(dap_global_db_instance_t *a_dbi, const char *a_mnemonim, dap_guuid_t a_guuid,
                                                   const char *a_group_mask, uint64_t a_ttl, bool a_owner_root_access,
                                                   dap_global_db_role_t a_default_role, dap_cluster_type_t a_links_cluster_role)
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
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    if (a_mnemonim)
        l_cluster->links_cluster = dap_cluster_by_mnemonim(a_mnemonim);
    if (!l_cluster->links_cluster) {
        l_cluster->links_cluster = dap_cluster_new(a_mnemonim, a_guuid, a_links_cluster_role);
        if (!l_cluster->links_cluster) {
            log_it(L_ERROR, "Can't create links cluster");
            DAP_DELETE(l_cluster);
            return NULL;
        }
    }
    l_cluster->role_cluster = dap_cluster_new(NULL, dap_guuid_compose(UINT64_MAX, UINT64_MAX), DAP_CLUSTER_TYPE_VIRTUAL);
    if (!l_cluster->role_cluster) {
        log_it(L_ERROR, "Can't create role cluster");
        dap_cluster_delete(l_cluster->links_cluster);
        DAP_DELETE(l_cluster);
        return NULL;
    }
    if (l_cluster->links_cluster &&
            (l_cluster->links_cluster->type == DAP_CLUSTER_TYPE_AUTONOMIC ||
            l_cluster->links_cluster->type == DAP_CLUSTER_TYPE_EMBEDDED)) {
        l_cluster->links_cluster->members_add_callback = dap_link_manager_add_links_cluster;
        l_cluster->links_cluster->members_delete_callback = dap_link_manager_remove_links_cluster;
    }
    l_cluster->groups_mask = dap_strdup(a_group_mask);
    if (!l_cluster->groups_mask) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        dap_cluster_delete(l_cluster->role_cluster);
        dap_cluster_delete(l_cluster->links_cluster);
        DAP_DELETE(l_cluster);
        return NULL;
    }
    l_cluster->ttl = a_dbi->store_time_limit ? a_ttl ? dap_min(a_dbi->store_time_limit, a_ttl) : a_dbi->store_time_limit : a_ttl;
    l_cluster->default_role = a_default_role;
    l_cluster->owner_root_access = a_owner_root_access;
    l_cluster->dbi = a_dbi;
    l_cluster->link_manager = dap_link_manager_get_default();
    l_cluster->sync_context.state = DAP_GLOBAL_DB_SYNC_STATE_START;
    DL_APPEND(a_dbi->clusters, l_cluster);
    if (dap_strcmp(DAP_STREAM_CLUSTER_LOCAL, a_mnemonim))
        dap_proc_thread_timer_add(NULL, s_gdb_cluster_sync_timer_callback, l_cluster, 1000);
    log_it(L_INFO, "Successfully added GlobalDB cluster ID %s for group mask %s, TTL %s",
                    dap_guuid_to_hex_str(a_guuid), a_group_mask, l_cluster->ttl ? dap_itoa(l_cluster->ttl) : "unlimited");
    return l_cluster;
}

dap_cluster_member_t *dap_global_db_cluster_member_add(dap_global_db_cluster_t *a_cluster, dap_stream_node_addr_t *a_node_addr, dap_global_db_role_t a_role)
{
    if (!a_cluster || !a_node_addr) {
        log_it(L_ERROR, "Invalid argument with cluster member adding");
        return NULL;
    }
    if (a_node_addr->uint64 == g_node_addr.uint64) {
        if (a_cluster->links_cluster->type == DAP_CLUSTER_TYPE_AUTONOMIC) {
            a_cluster->role_cluster->members_add_callback = dap_link_manager_add_static_links_cluster;
            a_cluster->role_cluster->members_delete_callback = dap_link_manager_remove_static_links_cluster;
            a_cluster->role_cluster->callbacks_arg = a_cluster->links_cluster;
        }
        dap_cluster_members_register(a_cluster->role_cluster);
    }
    return dap_cluster_member_add(a_cluster->role_cluster, a_node_addr, a_role, NULL);
}

void dap_global_db_cluster_delete(dap_global_db_cluster_t *a_cluster)
{
    //if (a_cluster->links_cluster)
    //    dap_cluster_delete(a_cluster->links_cluster);
    // TODO make a reference counter for cluster mnemonims
    if (!a_cluster) return; //happens when no network connection available
    
    // CRITICAL: Only delete role_cluster if it's initialized and valid
    // Check: not NULL and dbi is still valid
    if (a_cluster->role_cluster &&
        a_cluster->dbi) {
        dap_cluster_delete(a_cluster->role_cluster);
    }
    
    DAP_DELETE(a_cluster->groups_mask);
    if (a_cluster->dbi && a_cluster->dbi->clusters) {
        DL_DELETE(a_cluster->dbi->clusters, a_cluster);
    }
    DAP_DELETE(a_cluster);
}

static bool s_db_cluster_notify_on_proc_thread(void *a_arg)
{
    dap_store_obj_t *l_store_obj = a_arg;
    dap_global_db_notifier_t l_notifier = *(dap_global_db_notifier_t *)l_store_obj->ext;
    l_notifier.callback_notify(l_store_obj, l_notifier.callback_arg);
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

static void s_ch_in_pkt_callback(dap_stream_ch_t *a_ch, uint8_t a_type, const void *a_data, size_t a_data_size, void *a_arg)
{
    dap_return_if_fail(a_arg);
    debug_if(g_dap_global_db_debug_more, L_DEBUG, "Got packet with message type %hhu size %zu from addr " NODE_ADDR_FP_STR,
                                                           a_type, a_data_size, NODE_ADDR_FP_ARGS_S(a_ch->stream->node));
    dap_global_db_cluster_t *l_cluster = a_arg;
    switch (a_type) {
    case DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_REQUEST: {
        dap_global_db_hash_pkt_t *l_pkt = (dap_global_db_hash_pkt_t *)a_data;
        dap_global_db_cluster_t *l_msg_cluster = dap_global_db_cluster_by_group(dap_global_db_instance_get_default(),
                                                                                (char *)l_pkt->group_n_hashses);
        if (l_msg_cluster == l_cluster) {
            debug_if(g_dap_global_db_debug_more, L_NOTICE, "Last activity for cluster %s was renewed", l_cluster->groups_mask);
            l_cluster->sync_context.stage_last_activity = dap_time_now();
        }
    } break;

    default:
        break;
    }
}

static void s_gdb_cluster_sync_timer_callback(void *a_arg)
{
    assert(a_arg);
    dap_global_db_cluster_t *l_cluster = a_arg;
    switch (l_cluster->sync_context.state) {
    case DAP_GLOBAL_DB_SYNC_STATE_START: {
        dap_stream_node_addr_t l_current_link = dap_cluster_get_random_link(l_cluster->links_cluster);
        if (dap_stream_node_addr_is_blank(&l_current_link))
            break;
        dap_list_t *l_groups = dap_global_db_driver_get_groups_by_mask(l_cluster->groups_mask);
        if (!l_groups) {    // Nothing to sync
            l_cluster->sync_context.state = DAP_GLOBAL_DB_SYNC_STATE_IDLE;
            l_cluster->sync_context.stage_last_activity = dap_time_now();
            break;
        }
        l_cluster->sync_context.current_link = l_current_link;
        dap_stream_ch_add_notifier(&l_current_link, DAP_STREAM_CH_GDB_ID, DAP_STREAM_PKT_DIR_IN, s_ch_in_pkt_callback, l_cluster);
        for (dap_list_t *it = l_groups; it; it = it->next) {
            if (!dap_global_db_driver_count(it->data, c_dap_global_db_driver_hash_blank, true))
                continue;   // Don't send request for empty group, if any
            size_t l_group_len = dap_strlen(it->data) + 1;
            dap_global_db_start_pkt_t *l_msg = DAP_NEW_STACK_SIZE(dap_global_db_start_pkt_t, sizeof(dap_global_db_start_pkt_t) + l_group_len);
            l_msg->last_hash = c_dap_global_db_driver_hash_blank; //dap_db_get_last_hash_remote(l_req->link, l_req->group);
            l_msg->group_len = l_group_len;
            memcpy(l_msg->group, it->data, l_group_len);
            debug_if(g_dap_global_db_debug_more, L_INFO, "OUT: GLOBAL_DB_SYNC_START packet for group %s from first record", l_msg->group);
            dap_stream_ch_pkt_send_by_addr(&l_current_link, DAP_STREAM_CH_GDB_ID, DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_START,
                                           l_msg, dap_global_db_start_pkt_get_size(l_msg));
        }

        dap_list_free_full(l_groups, NULL);
        
        l_cluster->sync_context.state = DAP_GLOBAL_DB_SYNC_STATE_IDLE;
        l_cluster->sync_context.stage_last_activity = dap_time_now();

    } break;
    case DAP_GLOBAL_DB_SYNC_STATE_IDLE:
        if (dap_time_now() - l_cluster->sync_context.stage_last_activity >
                l_cluster->dbi->sync_idle_time) {
            l_cluster->sync_context.state = DAP_GLOBAL_DB_SYNC_STATE_START;
            if (!dap_stream_node_addr_is_blank(&l_cluster->sync_context.current_link))
                dap_stream_ch_del_notifier(&l_cluster->sync_context.current_link, DAP_STREAM_CH_GDB_ID,
                                           DAP_STREAM_PKT_DIR_IN, s_ch_in_pkt_callback, l_cluster);
            l_cluster->sync_context.current_link = (dap_stream_node_addr_t){};
        }
        break;
    default:
        break;
    }
}
