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

#include <errno.h>
#include <stdatomic.h>
#include <time.h>
#include "dap_common.h"
#include "dap_global_db.h"
#include "dap_global_db_cluster.h"
#include "dap_global_db.h"
#include "dap_global_db_pkt.h"
#include "dap_global_db_ch.h"
#include "dap_link_manager.h"
#include "dap_stream_ch_gossip.h"
#include "dap_strfuncs.h"
#include "dap_proc_thread.h"
#include "dap_hash.h"
#include "dap_dl.h"

#define LOG_TAG "dap_global_db_cluster"

static void s_gdb_cluster_sync_timer_callback(void *a_arg);
static void s_cluster_free(dap_global_db_cluster_t *l_cluster);
static void s_ch_in_pkt_callback(dap_stream_ch_t *a_ch, uint8_t a_type, const void *a_data, size_t a_data_size, void *a_arg);

static dap_global_db_cluster_t *s_local_cluster = NULL, *s_global_cluster = NULL;

int dap_global_db_cluster_init()
{
    dap_global_db_ch_init();
    // Pseudo-cluster for global scope
    if ( !(s_global_cluster = dap_global_db_cluster_add(
                dap_global_db_instance_get_default(), DAP_CLUSTER_GLOBAL,
                *(dap_guuid_t*)&uint128_0, DAP_GLOBAL_DB_CLUSTER_GLOBAL,
                dap_config_get_item_uint64_default(g_config, "global_db", "ttl_unclustered", DAP_GLOBAL_DB_UNCLUSTERED_TTL),
                true, DAP_GDB_MEMBER_ROLE_GUEST, DAP_CLUSTER_TYPE_SYSTEM)))
        return -1;

    // Pseudo-cluster for local scope (unsynced groups).
    if ( !(s_local_cluster = dap_global_db_cluster_add(
                dap_global_db_instance_get_default(), DAP_CLUSTER_LOCAL,
                dap_guuid_compose(0, 1), DAP_GLOBAL_DB_CLUSTER_LOCAL,
                0, false, DAP_GDB_MEMBER_ROLE_NOBODY, DAP_CLUSTER_TYPE_SYSTEM)))
        return -2;

    log_it(L_NOTICE, "Adding node addr " NODE_ADDR_FP_STR " to local cluster", NODE_ADDR_FP_ARGS_S(g_node_addr));
    dap_global_db_cluster_member_add(s_local_cluster, &g_node_addr, DAP_GDB_MEMBER_ROLE_ROOT);

    return 0;
}

/* confcall W55-F3: deinit barrier.  cluster_delete posts each cluster's
 * FREE to a proc thread; the dbi is freed right after this function by
 * dap_global_db_instance_deinit — a finalizer running later would touch a
 * dead dbi, one never running (proc thread stopped first) would leak the
 * cluster.  Post a barrier to EVERY proc thread and wait: their FIFOs
 * guarantee every previously-posted finalizer completed. */
struct s_deinit_barrier { pthread_mutex_t lock; pthread_cond_t cond; unsigned pending; };

static bool s_deinit_barrier_cb(void *a_arg)
{
    struct s_deinit_barrier *l_b = a_arg;
    pthread_mutex_lock(&l_b->lock);
    if (--l_b->pending == 0)
        pthread_cond_broadcast(&l_b->cond);
    pthread_mutex_unlock(&l_b->lock);
    return false;
}

void dap_global_db_cluster_deinit()
{
    dap_global_db_instance_t *l_dbi = dap_global_db_instance_get_default();
    if (!l_dbi)
        return;
    /* snapshot under the lock — each delete unlinks under the same lock */
    dap_global_db_cluster_t *it, *tmp;
    dap_dl_foreach_safe(l_dbi->clusters, it, tmp)
        dap_global_db_cluster_delete(it);

    struct s_deinit_barrier l_b = { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER, .pending = 0 };
    uint32_t l_n = dap_proc_thread_get_count();
    pthread_mutex_lock(&l_b.lock);
    for (uint32_t i = 0; i < l_n; i++) {
        dap_proc_thread_t *l_t = dap_proc_thread_get(i);
        if (l_t && dap_proc_thread_callback_add_pri(l_t, s_deinit_barrier_cb, &l_b, DAP_QUEUE_MSG_PRIORITY_NORMAL) == 0)
            l_b.pending++;
    }
    /* bounded wait: a proc thread that already stopped cannot ack; its
     * queue was discarded by the SDK (leak, not UAF — the finalizer never
     * runs).  5 s is far beyond any pending GDB callback. */
    struct timespec l_deadline;
    clock_gettime(CLOCK_REALTIME, &l_deadline);
    l_deadline.tv_sec += 5;
    while (l_b.pending > 0)
        if (pthread_cond_timedwait(&l_b.cond, &l_b.lock, &l_deadline) == ETIMEDOUT) {
            log_it(L_WARNING, "GlobalDB cluster deinit: %u proc thread(s) did not drain in time", l_b.pending);
            break;
        }
    pthread_mutex_unlock(&l_b.lock);
}

dap_global_db_cluster_t *dap_global_db_cluster_by_group(dap_global_db_instance_t *a_dbi, const char *a_group_name)
{
    /* confcall W55-F2: the list is mutated by add (caller thread) and by
     * the delete-unlink (caller thread) while proc/GDB threads walk it
     * here — read under the rwlock.  The returned pointer's lifetime is
     * the same as before this lock existed: a cluster is unlinked
     * synchronously in dap_global_db_cluster_delete (W55-F1), so a match
     * found here can only race a delete that started after our unlock. */
    dap_global_db_cluster_t *it, *l_ret = NULL;
    pthread_rwlock_rdlock(&a_dbi->clusters_lock);
    dap_dl_foreach(a_dbi->clusters, it)
        if (dap_global_db_group_match_mask(a_group_name, it->groups_mask)) {
            l_ret = it;
            /* W56-F1: pin under the same lock the unlink takes — a delete
             * that starts after our unlock can no longer free it under us */
            atomic_fetch_add_explicit(&it->refs, 1, memory_order_acq_rel);
            break;
        }
    pthread_rwlock_unlock(&a_dbi->clusters_lock);
    return l_ret;
}

void dap_global_db_cluster_unref(dap_global_db_cluster_t *a_cluster)
{
    if (!a_cluster)
        return;
    if (atomic_fetch_sub_explicit(&a_cluster->refs, 1, memory_order_acq_rel) == 1)
        s_cluster_free(a_cluster);
}

void dap_global_db_cluster_broadcast(dap_global_db_cluster_t *a_cluster, dap_global_db_store_obj_t *a_store_obj)
{
    dap_global_db_pkt_t *l_pkt = dap_global_db_pkt_serialize(a_store_obj);
    union hash_convert {
        dap_hash_sha3_256_t gossip_hash;
        dap_global_db_hash_t gdb_hash;
    } l_hash_cvt = {};
    l_hash_cvt.gdb_hash = dap_global_db_hash_get(a_store_obj);
    dap_gossip_msg_issue(a_cluster->links_cluster, DAP_STREAM_CH_GDB_ID, l_pkt, dap_global_db_pkt_get_size(l_pkt), &l_hash_cvt.gossip_hash);
    DAP_DELETE(l_pkt);
}

dap_global_db_cluster_t *dap_global_db_cluster_add(dap_global_db_instance_t *a_dbi, const char *a_mnemonim, dap_guuid_t a_guuid,
                                                   const char *a_group_mask, uint64_t a_ttl, bool a_owner_root_access,
                                                   dap_global_db_role_t a_default_role, dap_cluster_type_t a_links_cluster_role)
{
    dap_global_db_cluster_t *it;
    pthread_rwlock_rdlock(&a_dbi->clusters_lock);
    dap_dl_foreach(a_dbi->clusters, it) {
        if (!dap_strcmp(it->groups_mask, a_group_mask)) {
            pthread_rwlock_unlock(&a_dbi->clusters_lock);
            log_it(L_WARNING, "Group mask '%s' already present in the list, ignore it", a_group_mask);
            return NULL;
        }
    }
    pthread_rwlock_unlock(&a_dbi->clusters_lock);
    dap_global_db_cluster_t *l_cluster = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_global_db_cluster_t, NULL);
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
    l_cluster->sync_context.state = DAP_GLOBAL_DB_SYNC_STATE_START;
    atomic_store_explicit(&l_cluster->refs, 1, memory_order_release);   /* W56-F1: the list's ref */
    pthread_rwlock_wrlock(&a_dbi->clusters_lock);   /* confcall W55-F2 */
    dap_dl_append(a_dbi->clusters, l_cluster);
    pthread_rwlock_unlock(&a_dbi->clusters_lock);
    if (dap_strcmp(DAP_CLUSTER_LOCAL, a_mnemonim))
        dap_proc_thread_timer_add_ex(NULL, s_gdb_cluster_sync_timer_callback, l_cluster, 1000, &l_cluster->sync_timer);
    log_it(L_INFO, "Successfully added GlobalDB cluster ID %s for group mask %s, TTL %s",
                    dap_guuid_to_hex_str(a_guuid), a_group_mask, l_cluster->ttl ? dap_itoa(l_cluster->ttl) : "unlimited");
    return l_cluster;
}

dap_cluster_member_t *dap_global_db_cluster_member_add(dap_global_db_cluster_t *a_cluster, dap_cluster_node_addr_t *a_node_addr, dap_global_db_role_t a_role)
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

/* confcall W54-F2: the actual teardown.  Runs on the sync timer's proc
 * thread (posted behind any in-flight sync callback — FIFO), or inline when
 * there was no timer / the post was impossible.  Also drops the peer-stream
 * notifier the sync state machine may have left registered with `l_cluster`
 * as its arg (the IDLE→START transition removes it, a delete mid-cycle did
 * not) — a GDB REQUEST from that link would otherwise invoke
 * s_ch_in_pkt_callback on the freed cluster. */
/* confcall W54-F2 / W55-F1: the FREE half of the teardown.  Runs on the
 * sync timer's proc thread (posted behind any in-flight sync callback —
 * FIFO), or inline when there was no timer / the post was impossible.  By
 * the time it runs the cluster is already UNLINKED from dbi->clusters and
 * from every stream notifier (done synchronously on the caller thread in
 * dap_global_db_cluster_delete), so nothing can find it any more — this
 * only releases memory. */
static bool s_cluster_delete_finalize(void *a_arg)
{
    /* W56-F1: this drops the LIST's reference; a by_group borrower still
     * mid-use keeps the struct alive and frees it on its own unref. */
    dap_global_db_cluster_unref((dap_global_db_cluster_t *)a_arg);
    return false;
}

static void s_cluster_free(dap_global_db_cluster_t *l_cluster)
{
    /* confcall W56-F3: the unlink-time notifier removal raced an in-flight
     * sync callback that SET current_link + registered the notifier after
     * the deleter's check; this runs strictly after that callback (FIFO on
     * its proc thread, or inline with no timer) — repeat the removal.  A
     * proc thread is never a stream worker, so the sync form is safe. */
    if (!dap_cluster_node_addr_is_blank(&l_cluster->sync_context.current_link)) {
        dap_stream_ch_del_notifier_sync(&l_cluster->sync_context.current_link, DAP_STREAM_CH_GDB_ID,
                                        DAP_STREAM_PKT_DIR_IN, s_ch_in_pkt_callback, l_cluster);
        l_cluster->sync_context.current_link = (dap_cluster_node_addr_t){};
    }
    dap_cluster_delete(l_cluster->role_cluster);
    DAP_DELETE(l_cluster->groups_mask);
    /* W55-F1: the notifier list (dap_global_db_cluster_add_notify_callback)
     * was never freed — and its callback_arg's owner (avrs cluster) is gone. */
    dap_global_db_notifier_t *l_n, *l_tmp;
    dap_dl_foreach_safe(l_cluster->notifiers, l_n, l_tmp) {
        dap_dl_delete(l_cluster->notifiers, l_n);
        DAP_DELETE(l_n);
    }
    DAP_DELETE(l_cluster);
}

/* confcall W55-F1/F2: synchronous UNLINK.  After this returns no new lookup
 * (by_group / notify / stream packet) can reach the cluster, so the owner
 * may free the objects its callbacks reference.  The memory itself is
 * released later by s_cluster_delete_finalize behind the in-flight sync
 * callback (W54-F2). */
static void s_cluster_unlink(dap_global_db_cluster_t *a_cluster)
{
    pthread_rwlock_wrlock(&a_cluster->dbi->clusters_lock);
    dap_dl_delete(a_cluster->dbi->clusters, a_cluster);
    pthread_rwlock_unlock(&a_cluster->dbi->clusters_lock);
    /* W54-F2/W55-F4: the peer-stream notifier the sync state machine may
     * have left registered with `a_cluster` as its arg — removed
     * SYNCHRONOUSLY (the async form returned before the worker unlinked
     * it; a GDB packet in that gap dispatched to the freed cluster). */
    if (!dap_cluster_node_addr_is_blank(&a_cluster->sync_context.current_link)) {
        dap_stream_ch_del_notifier_sync(&a_cluster->sync_context.current_link, DAP_STREAM_CH_GDB_ID,
                                        DAP_STREAM_PKT_DIR_IN, s_ch_in_pkt_callback, a_cluster);
        a_cluster->sync_context.current_link = (dap_cluster_node_addr_t){};
    }
}

void dap_global_db_cluster_delete(dap_global_db_cluster_t *a_cluster)
{
    //if (a_cluster->links_cluster)
    //    dap_cluster_delete(a_cluster->links_cluster);
    // TODO make a reference counter for cluster mnemonims
    if (!a_cluster) return; //happens when no network connection available
    /* confcall W53-F8: the 1 s sync timer registered in _add() was never
     * stopped — every deleted cluster (per-room AVRS/chat clusters in
     * ConfCall churn constantly) left a repeating callback dereferencing
     * this freed struct forever.
     * W54-F2: cancel alone is NOT a drain — a sync callback that already
     * passed its cancelled check may be mid-flight on the proc thread doing
     * GDB/driver I/O against this struct for tens of ms.  Post the FREE to
     * that same proc thread: the per-thread FIFO guarantees it runs after
     * the in-flight hop returned.
     * W55-F1: but UNLINK synchronously here — while the cluster stayed in
     * dbi->clusters until the finalizer ran, a store for its group could
     * still resolve it and post its notifiers with a callback_arg (the avrs
     * cluster) the caller had already freed. */
    s_cluster_unlink(a_cluster);
    if (a_cluster->sync_timer) {
        dap_proc_thread_timer_t l_timer = a_cluster->sync_timer;
        a_cluster->sync_timer = NULL;
        if (dap_proc_thread_timer_cancel_then(l_timer, s_cluster_delete_finalize, a_cluster) == 0)
            return;   /* finalizer owns a_cluster now */
        /* post impossible (module deinit / OOM): no proc-thread callback can
         * be mid-flight during deinit; fall through to inline teardown */
    }
    s_cluster_delete_finalize(a_cluster);
}

static bool s_db_cluster_notify_on_proc_thread(void *a_arg)
{
    dap_global_db_store_obj_t *l_store_obj = a_arg;
    dap_global_db_notifier_t l_notifier = *(dap_global_db_notifier_t *)l_store_obj->ext;
    l_notifier.callback_notify(l_store_obj, l_notifier.callback_arg);
    dap_global_db_store_obj_free_one(l_store_obj);
    return false;
}

void dap_global_db_cluster_notify(dap_global_db_cluster_t *a_cluster, dap_global_db_store_obj_t *a_store_obj)
{
    dap_global_db_notifier_t *l_notifier;
    dap_dl_foreach(a_cluster->notifiers, l_notifier) {
        assert(l_notifier->callback_notify);
        dap_global_db_store_obj_t *l_store_obj = dap_global_db_store_obj_copy_ext(a_store_obj, l_notifier, sizeof(*l_notifier));
        dap_proc_thread_callback_add_pri(NULL, s_db_cluster_notify_on_proc_thread, l_store_obj, DAP_QUEUE_MSG_PRIORITY_LOW);
    }
}

int dap_global_db_cluster_add_notify_callback(dap_global_db_cluster_t *a_cluster, dap_global_db_store_obj_callback_notify_t a_callback, void *a_callback_arg)
{
    dap_return_val_if_fail(a_cluster && a_callback, -1);
    dap_global_db_notifier_t *l_notifier = DAP_NEW_Z(dap_global_db_notifier_t);
    if (!l_notifier) {
        log_it(L_CRITICAL, "Not enough memory");
        return -2;
    }
    l_notifier->callback_notify = a_callback;
    l_notifier->callback_arg = a_callback_arg;
    dap_dl_append(a_cluster->notifiers, l_notifier);
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
        if (a_data_size < DAP_GLOBAL_DB_HASH_PKT_HDR_WIRE_SIZE)
            break;
        dap_global_db_hash_pkt_hdr_mem_t l_hmem;
        if (dap_global_db_hash_pkt_hdr_unpack((const uint8_t *)a_data, a_data_size, &l_hmem) != 0 ||
                a_data_size != dap_global_db_hash_pkt_get_size_hdr(&l_hmem))
            break;
        dap_global_db_hash_pkt_t *l_pkt = (dap_global_db_hash_pkt_t *)a_data;
        dap_global_db_cluster_t *l_msg_cluster = dap_global_db_cluster_by_group(dap_global_db_instance_get_default(),
                                                                                (char *)l_pkt->group_n_hashses);
        if (l_msg_cluster == l_cluster) {
            debug_if(g_dap_global_db_debug_more, L_NOTICE, "Last activity for cluster %s was renewed", l_cluster->groups_mask);
            l_cluster->sync_context.stage_last_activity = dap_time_now();
        }
        dap_global_db_cluster_unref(l_msg_cluster);   /* W56-F1 */
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
        dap_cluster_node_addr_t l_current_link = dap_cluster_get_random_link(l_cluster->links_cluster);
        if (dap_cluster_node_addr_is_blank(&l_current_link))
            break;
        dap_list_t *l_groups = dap_global_db_get_groups_by_mask(l_cluster->groups_mask);
        // For an explicit (non-wildcard) cluster mask, initiate sync even when
        // the local group does not exist yet. Without this, a fresh node that
        // has never written into the group (e.g. MASTER for
        // confcall-stagenet.nodes.list) never sends START, the peer never
        // replies with HASHES, and the group stays empty forever
        // (chicken-and-egg: GDB sync wants a local group, but the local group
        // is only created on first write — which itself only comes through
        // sync). Sending START with last_hash=blank makes the peer dump
        // everything it has for that exact group, which is what we need on
        // bootstrap. The "bootstrap" entry is flagged so the loop below skips
        // the empty-group filter for it only.
        bool l_bootstrap_literal = !l_groups && !strpbrk(l_cluster->groups_mask, "*?[");
        if (l_bootstrap_literal)
            l_groups = dap_list_append(NULL, dap_strdup(l_cluster->groups_mask));
        if (!l_groups) {
            l_cluster->sync_context.state = DAP_GLOBAL_DB_SYNC_STATE_IDLE;
            l_cluster->sync_context.stage_last_activity = dap_time_now();
            break;
        }
        l_cluster->sync_context.current_link = l_current_link;
        dap_stream_ch_add_notifier(&l_current_link, DAP_STREAM_CH_GDB_ID, DAP_STREAM_PKT_DIR_IN, s_ch_in_pkt_callback, l_cluster);
        for (dap_list_t *it = l_groups; it; it = it->next) {
            if (!l_bootstrap_literal && !dap_global_db_group_count(it->data, true))
                continue;
            size_t l_group_len = dap_strlen(it->data) + 1;
            size_t l_pkt_total = DAP_GLOBAL_DB_START_PKT_HDR_WIRE_SIZE + l_group_len;
            byte_t *l_msg_buf = DAP_NEW_STACK_SIZE(byte_t, l_pkt_total);
            dap_global_db_start_pkt_hdr_mem_t l_st_hdr = { .group_len = (uint16_t)l_group_len };
            memcpy(l_st_hdr.last_hash, &c_dap_global_db_hash_blank, sizeof(l_st_hdr.last_hash));
            if (dap_global_db_start_pkt_hdr_pack(&l_st_hdr, l_msg_buf, DAP_GLOBAL_DB_START_PKT_HDR_WIRE_SIZE) != 0) {
                log_it(L_ERROR, "GLOBAL_DB start header pack failed");
                continue;
            }
            memcpy(l_msg_buf + DAP_GLOBAL_DB_START_PKT_HDR_WIRE_SIZE, it->data, l_group_len);
            debug_if(g_dap_global_db_debug_more, L_INFO, "OUT: GLOBAL_DB_SYNC_START packet for group %s from first record", (const char *)it->data);
            dap_stream_ch_pkt_send_by_addr(&l_current_link, DAP_STREAM_CH_GDB_ID, DAP_STREAM_CH_GLOBAL_DB_MSG_TYPE_START,
                                           l_msg_buf, l_pkt_total);
        }

        dap_list_free_full(l_groups, NULL);

        l_cluster->sync_context.state = DAP_GLOBAL_DB_SYNC_STATE_IDLE;
        l_cluster->sync_context.stage_last_activity = dap_time_now();

    } break;
    case DAP_GLOBAL_DB_SYNC_STATE_IDLE:
        if (dap_time_now() - l_cluster->sync_context.stage_last_activity >
                l_cluster->dbi->sync_idle_time) {
            l_cluster->sync_context.state = DAP_GLOBAL_DB_SYNC_STATE_START;
            if (!dap_cluster_node_addr_is_blank(&l_cluster->sync_context.current_link))
                dap_stream_ch_del_notifier(&l_cluster->sync_context.current_link, DAP_STREAM_CH_GDB_ID,
                                           DAP_STREAM_PKT_DIR_IN, s_ch_in_pkt_callback, l_cluster);
            l_cluster->sync_context.current_link = (dap_cluster_node_addr_t){};
        }
        break;
    default:
        break;
    }
}
