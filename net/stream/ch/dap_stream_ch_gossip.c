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
#include <string.h>
#include "dap_events.h"
#include "dap_strfuncs.h"
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_ch_gossip.h"
#include "dap_stream_cluster.h"

#define LOG_TAG "dap_stream_ch_gossip"

static struct gossip_callback {
    uint8_t ch_id;
    dap_gossip_callback_payload_t callback_payload;
    struct gossip_callback *prev, *next;
} *s_gossip_callbacks_list = NULL;

static pthread_rwlock_t s_gossip_lock = PTHREAD_RWLOCK_INITIALIZER;
static struct gossip_msg_item {
    dap_hash_t payload_hash;
    dap_nanotime_t timestamp;
    bool with_payload;
    UT_hash_handle hh;
    byte_t message[];
} *s_gossip_last_msgs = NULL;
dap_timerfd_t *s_gossip_timer = NULL;

static bool s_callback_hashtable_maintenance(void *a_arg);
static bool s_stream_ch_packet_in(dap_stream_ch_t *a_ch, void *a_arg);
static bool s_debug_more = false;
/**
 * @brief dap_stream_ch_gdb_init
 * @return
 */
int dap_stream_ch_gossip_init()
{
    s_debug_more = dap_config_get_item_bool_default(g_config, "gossip", "debug_more", s_debug_more);
    log_it(L_NOTICE, "GOSSIP epidemic protocol channel initialized");
    dap_stream_ch_proc_add(DAP_STREAM_CH_GOSSIP_ID, NULL, NULL, s_stream_ch_packet_in, NULL);
    s_gossip_timer = dap_timerfd_start(1000, s_callback_hashtable_maintenance, NULL);
    return 0;
}

void dap_stream_ch_gossip_deinit()
{
    if (s_gossip_timer)
        dap_timerfd_delete(s_gossip_timer->worker, s_gossip_timer->esocket_uuid);
    pthread_rwlock_wrlock(&s_gossip_lock);
    struct gossip_msg_item *it, *tmp;
    HASH_ITER(hh, s_gossip_last_msgs, it, tmp) {
        HASH_DEL(s_gossip_last_msgs, it);
        DAP_DELETE(it);
    }
    pthread_rwlock_unlock(&s_gossip_lock);
}

static struct gossip_callback *s_get_callbacks_by_ch_id(const char a_ch_id)
{
    struct gossip_callback *l_callback;
    DL_FOREACH(s_gossip_callbacks_list, l_callback)
        if (l_callback->ch_id == a_ch_id)
            return l_callback;
    return NULL;
}

int dap_stream_ch_gossip_callback_add(const char a_ch_id, dap_gossip_callback_payload_t a_callback)
{
    if (s_get_callbacks_by_ch_id(a_ch_id)) {
        log_it(L_ERROR, "Channel '%c' already set gossip callback. Alone callback per channel is allowed", a_ch_id);
        return -1;
    }
    struct gossip_callback *l_callback_new = DAP_NEW_Z(struct gossip_callback);
    if (!l_callback_new) {
        log_it(L_CRITICAL, "Not enough memory");
        return -2;
    }
    l_callback_new->ch_id = a_ch_id;
    l_callback_new->callback_payload = a_callback;
    DL_APPEND(s_gossip_callbacks_list, l_callback_new);
    log_it(L_INFO, "Successfully added gossip callback for channel '%c'", a_ch_id);
    return 0;
}

static bool s_callback_hashtable_maintenance(void UNUSED_ARG *a_arg)
{
    pthread_rwlock_wrlock(&s_gossip_lock);
    dap_nanotime_t l_time_now = dap_nanotime_now();
    struct gossip_msg_item *it, *tmp;
    HASH_ITER(hh, s_gossip_last_msgs, it, tmp) {
        if (l_time_now - it->timestamp > DAP_GOSSIP_LIFETIME * 1000000000UL) {
            HASH_DEL(s_gossip_last_msgs, it);
            DAP_DELETE(it);
        }
    }
    pthread_rwlock_unlock(&s_gossip_lock);
    return true;
}

void dap_gossip_msg_issue(dap_cluster_t *a_cluster, const char a_ch_id, const void *a_payload, size_t a_payload_size, dap_hash_fast_t *a_payload_hash)
{
    dap_return_if_fail(a_cluster && a_payload && a_payload_size && a_payload_hash);
    if (dap_cluster_is_empty(a_cluster))
        return;
    struct gossip_msg_item *l_msg_item = NULL;
    pthread_rwlock_wrlock(&s_gossip_lock);
    unsigned l_hash_value = 0;
    HASH_VALUE(a_payload_hash, sizeof(dap_hash_t), l_hash_value);
    HASH_FIND_BYHASHVALUE(hh, s_gossip_last_msgs, a_payload_hash, sizeof(dap_hash_t), l_hash_value, l_msg_item);
    if (l_msg_item) {
        pthread_rwlock_unlock(&s_gossip_lock);
        log_it(L_ERROR, "Hash %s already exist", dap_hash_fast_to_str_static(a_payload_hash)); 
        return;
    }
    l_msg_item = DAP_NEW_Z_SIZE(struct gossip_msg_item, sizeof(struct gossip_msg_item) + sizeof(dap_gossip_msg_t) +
                                                        sizeof(g_node_addr) + a_payload_size);
    if (!l_msg_item) {
        pthread_rwlock_unlock(&s_gossip_lock);
        log_it(L_CRITICAL, "Insufficient memory");
        return;
    }
    l_msg_item->payload_hash = *a_payload_hash;
    l_msg_item->timestamp = dap_nanotime_now();
    l_msg_item->with_payload = true;
    dap_gossip_msg_t *l_msg = (dap_gossip_msg_t *)l_msg_item->message;
    l_msg->version = DAP_GOSSIP_CURRENT_VERSION;
    l_msg->payload_ch_id = a_ch_id;
    l_msg->trace_len = sizeof(g_node_addr);
    l_msg->payload_len = a_payload_size;
    l_msg->cluster_id.raw = a_cluster->guuid.raw;
    l_msg->payload_hash = *a_payload_hash;
    *(dap_stream_node_addr_t *)l_msg->trace_n_payload = g_node_addr;
    memcpy(l_msg->trace_n_payload + l_msg->trace_len, a_payload, a_payload_size);
    HASH_ADD_BYHASHVALUE(hh, s_gossip_last_msgs, payload_hash, sizeof(dap_hash_t), l_hash_value, l_msg_item);
    pthread_rwlock_unlock(&s_gossip_lock);
    debug_if(s_debug_more, L_INFO, "OUT: GOSSIP_HASH packet for hash %s", dap_hash_fast_to_str_static(a_payload_hash));
    dap_cluster_broadcast(a_cluster, DAP_STREAM_CH_GOSSIP_ID,
                          DAP_STREAM_CH_GOSSIP_MSG_TYPE_HASH,
                          a_payload_hash, sizeof(dap_hash_t),
                          &g_node_addr, 1);
}

static bool s_stream_ch_packet_in(dap_stream_ch_t *a_ch, void *a_arg)
{
    dap_stream_ch_pkt_t *l_ch_pkt = (dap_stream_ch_pkt_t *)a_arg;
    switch (l_ch_pkt->hdr.type) {

    case DAP_STREAM_CH_GOSSIP_MSG_TYPE_HASH:
    case DAP_STREAM_CH_GOSSIP_MSG_TYPE_REQUEST: {
        struct gossip_msg_item *l_msg_item = NULL;
        if (l_ch_pkt->hdr.data_size != sizeof(dap_hash_t)) {
            log_it(L_WARNING, "Incorrect gossip message data size %u, expected %zu",
                                        l_ch_pkt->hdr.data_size, sizeof(dap_hash_t));
            return false;
        }
        dap_hash_fast_t *l_payload_hash = (dap_hash_fast_t *)&l_ch_pkt->data;
        debug_if(s_debug_more, L_INFO, "IN: %s packet for hash %s", l_ch_pkt->hdr.type == DAP_STREAM_CH_GOSSIP_MSG_TYPE_HASH
                                                                    ? "GOSSIP_HASH" : "GOSSIP_REQUEST",
                                                                    dap_hash_fast_to_str_static(l_payload_hash));
        unsigned l_hash_value = 0;
        HASH_VALUE(l_payload_hash, sizeof(dap_hash_t), l_hash_value);
        pthread_rwlock_wrlock(&s_gossip_lock);
        HASH_FIND_BYHASHVALUE(hh, s_gossip_last_msgs, l_ch_pkt->data, sizeof(dap_hash_t), l_hash_value, l_msg_item);
        if (l_msg_item) {
            if (l_msg_item->timestamp < dap_nanotime_now() - DAP_GOSSIP_LIFETIME * 1000000000UL) {
                debug_if(s_debug_more, L_INFO, "Packet for hash %s is derelict", dap_hash_fast_to_str_static(l_payload_hash));
                HASH_DEL(s_gossip_last_msgs, l_msg_item);
                DAP_DELETE(l_msg_item);
            } else if (l_msg_item->with_payload && l_ch_pkt->hdr.type == DAP_STREAM_CH_GOSSIP_MSG_TYPE_REQUEST) {
                debug_if(s_debug_more, L_INFO, "OUT: GOSSIP_DATA packet for hash %s", dap_hash_fast_to_str_static((dap_hash_fast_t *)&l_ch_pkt->data));
                // Send data associated with this hash by request
                dap_gossip_msg_t *l_msg = (dap_gossip_msg_t *)l_msg_item->message;
                dap_stream_ch_pkt_write_unsafe(a_ch, DAP_STREAM_CH_GOSSIP_MSG_TYPE_DATA, l_msg, dap_gossip_msg_get_size(l_msg));
            }
        } else if (l_ch_pkt->hdr.type == DAP_STREAM_CH_GOSSIP_MSG_TYPE_HASH) {
            struct gossip_msg_item *l_item_new = DAP_NEW_Z(struct gossip_msg_item);
            if (!l_item_new) {
                log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                pthread_rwlock_unlock(&s_gossip_lock);
                break;
            }
            l_item_new->payload_hash = *l_payload_hash;
            l_item_new->timestamp = dap_nanotime_now();
            HASH_ADD_BYHASHVALUE(hh, s_gossip_last_msgs, payload_hash, sizeof(dap_hash_t), l_hash_value, l_item_new);
            debug_if(s_debug_more, L_INFO, "OUT: GOSSIP_REQUEST packet for hash %s", dap_hash_fast_to_str_static(l_payload_hash));
            // Send request for data associated with this hash
            dap_stream_ch_pkt_write_unsafe(a_ch, DAP_STREAM_CH_GOSSIP_MSG_TYPE_REQUEST, l_payload_hash, sizeof(dap_hash_t));
        }
        pthread_rwlock_unlock(&s_gossip_lock);
    } break;

    case DAP_STREAM_CH_GOSSIP_MSG_TYPE_DATA: {
        dap_gossip_msg_t *l_msg = (dap_gossip_msg_t *)l_ch_pkt->data;
        if (l_ch_pkt->hdr.data_size < sizeof(dap_gossip_msg_t)) {
            log_it(L_WARNING, "Incorrect gossip message data size %u, must be at least %zu",
                                                l_ch_pkt->hdr.data_size, sizeof(dap_gossip_msg_t));
            return false;
        }
        if (l_ch_pkt->hdr.data_size != dap_gossip_msg_get_size(l_msg)) {
            log_it(L_WARNING, "Incorrect gossip message data size %u, expected %zu",
                                                l_ch_pkt->hdr.data_size, dap_gossip_msg_get_size(l_msg));
            return false;
        }
        if (l_msg->version != DAP_GOSSIP_CURRENT_VERSION) {
            log_it(L_ERROR, "Incorrect gossip protocol version %hhu, current version is %u",
                                                         l_msg->version, DAP_GOSSIP_CURRENT_VERSION);
            return false;
        }
        if (l_msg->trace_len % sizeof(dap_stream_node_addr_t) != 0) {
            log_it(L_WARNING, "Unaligned gossip message tracepath size %u", l_msg->trace_len);
            return false;
        }
        if (!l_msg->payload_len) {
            log_it(L_WARNING, "Zero size of gossip message payload");
            return false;
        }
        debug_if(s_debug_more, L_INFO, "IN: GOSSIP_DATA packet for hash %s", dap_hash_fast_to_str_static(&l_msg->payload_hash));
        unsigned l_hash_value = 0;
        HASH_VALUE(&l_msg->payload_hash, sizeof(dap_hash_t), l_hash_value);
        struct gossip_msg_item *l_payload_item = NULL, *l_payload_item_new;
        pthread_rwlock_wrlock(&s_gossip_lock);
        HASH_FIND_BYHASHVALUE(hh, s_gossip_last_msgs, &l_msg->payload_hash, sizeof(dap_hash_t), l_hash_value, l_payload_item);
        if (!l_payload_item || l_payload_item->with_payload) {
            // Get data for non requested hash or double data. Drop it
            pthread_rwlock_unlock(&s_gossip_lock);
            break;
        }
        if (l_payload_item->timestamp < dap_nanotime_now() - DAP_GOSSIP_LIFETIME * 1000000000UL) {
            HASH_DEL(s_gossip_last_msgs, l_payload_item);
            DAP_DELETE(l_payload_item);
            pthread_rwlock_unlock(&s_gossip_lock);
            break;
        }
        dap_cluster_t *l_links_cluster = dap_cluster_find(l_msg->cluster_id);
        if (l_links_cluster) {
            dap_cluster_member_t *l_check = dap_cluster_member_find_unsafe(l_links_cluster, &a_ch->stream->node);
            if (!l_check) {
                log_it(L_WARNING, "Node with addr "NODE_ADDR_FP_STR" isn't a member of cluster %s",
                                            NODE_ADDR_FP_ARGS_S(a_ch->stream->node), l_links_cluster->mnemonim);
                dap_stream_node_addr_t l_member = dap_cluster_get_random_link(l_links_cluster);
                if (dap_stream_node_addr_is_blank(&l_member)) {
                    log_it(L_ERROR, "Cluster %s has no active members", l_links_cluster->mnemonim);
                    pthread_rwlock_unlock(&s_gossip_lock);
                    break;
                }
                debug_if(s_debug_more, L_INFO, "OUT: GOSSIP_REQUEST packet for hash %s", dap_hash_fast_to_str_static(&l_msg->payload_hash));
                // Send request for data associated with this hash to another link
                dap_stream_ch_pkt_send_by_addr(&l_member, DAP_STREAM_CH_GOSSIP_ID, DAP_STREAM_CH_GOSSIP_MSG_TYPE_REQUEST,
                                               l_ch_pkt->data, sizeof(dap_hash_t));
                pthread_rwlock_unlock(&s_gossip_lock);
                break;
            }
        } else if (!IS_ZERO_128(l_msg->cluster_id.raw)) {
            const char *l_guuid_str = dap_guuid_to_hex_str(l_msg->cluster_id);
            log_it(L_ERROR, "Can't find cluster with ID %s for gossip message broadcasting", l_guuid_str);
            pthread_rwlock_unlock(&s_gossip_lock);
            break;
        }
        size_t l_payload_item_size = dap_gossip_msg_get_size(l_msg) + sizeof(g_node_addr) + sizeof(struct gossip_msg_item);
        HASH_DEL(s_gossip_last_msgs, l_payload_item);
        l_payload_item_new = DAP_REALLOC(l_payload_item, l_payload_item_size);
        if (!l_payload_item_new) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            pthread_rwlock_unlock(&s_gossip_lock);
            break;
        }
        l_payload_item = l_payload_item_new;
        HASH_ADD_BYHASHVALUE(hh, s_gossip_last_msgs, payload_hash, sizeof(dap_hash_t), l_hash_value, l_payload_item);
        l_payload_item->with_payload = true;
        // Copy message and append g_node_addr to pathtrace
        dap_gossip_msg_t *l_msg_new = (dap_gossip_msg_t *)l_payload_item->message;
        memcpy(l_msg_new, l_msg, sizeof(dap_gossip_msg_t) + l_msg->trace_len);
        l_msg_new->trace_len = l_msg->trace_len + sizeof(g_node_addr);
        *(dap_stream_node_addr_t *)(l_msg_new->trace_n_payload + l_msg->trace_len) = g_node_addr;
        memcpy(l_msg_new->trace_n_payload + l_msg_new->trace_len, l_msg->trace_n_payload + l_msg->trace_len, l_msg->payload_len);
        // Broadcast new message
        debug_if(s_debug_more, L_INFO, "OUT: GOSSIP_HASH broadcast for hash %s",
                                        dap_hash_fast_to_str_static(&l_msg_new->payload_hash));
        dap_cluster_broadcast(l_links_cluster, DAP_STREAM_CH_GOSSIP_ID,
                              DAP_STREAM_CH_GOSSIP_MSG_TYPE_HASH,
                              &l_msg_new->payload_hash, sizeof(dap_hash_t),
                              (dap_stream_node_addr_t *)l_msg_new->trace_n_payload,
                              l_msg_new->trace_len / sizeof(dap_stream_node_addr_t));
        pthread_rwlock_unlock(&s_gossip_lock);
        // Call back the payload func if any
        struct gossip_callback *l_callback = s_get_callbacks_by_ch_id(l_msg->payload_ch_id);
        if (!l_callback) {
            log_it(L_ERROR, "Can't find channel callback for channel '%c' to gossip message apply", l_msg->payload_ch_id);
            break;
        }
        assert(l_callback->callback_payload);
        l_callback->callback_payload(l_msg->trace_n_payload + l_msg->trace_len, l_msg->payload_len, a_ch->stream->node);
    } break;

    default:
        log_it(L_WARNING, "Unknown gossip packet type %hhu", l_ch_pkt->hdr.type);
        return false;
    }

    return true;
}

