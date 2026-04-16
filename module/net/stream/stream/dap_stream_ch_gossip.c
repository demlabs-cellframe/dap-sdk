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
#include "dap_cluster.h"
#include "dap_ht.h"
#include "dap_dl.h"
#include "dap_serialize.h"

#define LOG_TAG "dap_stream_ch_gossip"

const dap_serialize_field_t g_dap_gossip_msg_fields[] = {
    {
        .name = "version",
        .type = DAP_SERIALIZE_TYPE_UINT8,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_gossip_msg_mem_t, version),
        .size = sizeof(uint8_t),
    },
    {
        .name = "payload_ch_id",
        .type = DAP_SERIALIZE_TYPE_UINT8,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_gossip_msg_mem_t, payload_ch_id),
        .size = sizeof(uint8_t),
    },
    {
        .name = "padding",
        .type = DAP_SERIALIZE_TYPE_BYTES_FIXED,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_gossip_msg_mem_t, padding),
        .size = 2,
    },
    {
        .name = "trace_len",
        .type = DAP_SERIALIZE_TYPE_UINT32,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_gossip_msg_mem_t, trace_len),
        .size = sizeof(uint32_t),
    },
    {
        .name = "payload_len",
        .type = DAP_SERIALIZE_TYPE_UINT64,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_gossip_msg_mem_t, payload_len),
        .size = sizeof(uint64_t),
    },
    {
        .name = "cluster_id",
        .type = DAP_SERIALIZE_TYPE_BYTES_FIXED,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_gossip_msg_mem_t, cluster_id),
        .size = sizeof(dap_guuid_t),
    },
    {
        .name = "payload_hash",
        .type = DAP_SERIALIZE_TYPE_BYTES_FIXED,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_gossip_msg_mem_t, payload_hash),
        .size = sizeof(dap_hash_t),
    },
};

const dap_serialize_schema_t g_dap_gossip_msg_schema = {
    .name = "dap_gossip_msg",
    .version = 1,
    .struct_size = sizeof(dap_gossip_msg_mem_t),
    .field_count = sizeof(g_dap_gossip_msg_fields) / sizeof(g_dap_gossip_msg_fields[0]),
    .fields = g_dap_gossip_msg_fields,
    .magic = DAP_GOSSIP_MSG_MAGIC,
    .validate_func = NULL,
};

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
    dap_ht_handle_t hh;
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
        dap_timerfd_delete_mt(s_gossip_timer->worker, s_gossip_timer->esocket_uuid);
    pthread_rwlock_wrlock(&s_gossip_lock);
    struct gossip_msg_item *it, *tmp;
    dap_ht_foreach(s_gossip_last_msgs, it, tmp) {
        dap_ht_del(s_gossip_last_msgs, it);
        DAP_DELETE(it);
    }
    pthread_rwlock_unlock(&s_gossip_lock);
}

static struct gossip_callback *s_get_callbacks_by_ch_id(const char a_ch_id)
{
    struct gossip_callback *l_callback;
    dap_dl_foreach(s_gossip_callbacks_list, l_callback)
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
    dap_dl_append(s_gossip_callbacks_list, l_callback_new);
    log_it(L_INFO, "Successfully added gossip callback for channel '%c'", a_ch_id);
    return 0;
}

static bool s_callback_hashtable_maintenance(void UNUSED_ARG *a_arg)
{
    pthread_rwlock_wrlock(&s_gossip_lock);
    dap_nanotime_t l_time_now = dap_nanotime_now();
    struct gossip_msg_item *it, *tmp;
    dap_ht_foreach(s_gossip_last_msgs, it, tmp) {
        if (l_time_now - it->timestamp > DAP_GOSSIP_LIFETIME * 1000000000UL) {
            dap_ht_del(s_gossip_last_msgs, it);
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
    unsigned l_hash_value = dap_ht_hash_value(a_payload_hash, sizeof(dap_hash_t));
    dap_ht_find_by_hashvalue(s_gossip_last_msgs, a_payload_hash, sizeof(dap_hash_t), l_hash_value, l_msg_item);
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
    dap_gossip_msg_mem_t l_hdr = {
        .version = DAP_GOSSIP_CURRENT_VERSION,
        .payload_ch_id = (uint8_t)a_ch_id,
        .trace_len = (uint32_t)sizeof(g_node_addr),
        .payload_len = a_payload_size,
        .cluster_id = a_cluster->guuid,
        .payload_hash = *(const dap_hash_t *)a_payload_hash,
    };
    memset(l_hdr.padding, 0, sizeof(l_hdr.padding));
    if (dap_gossip_msg_hdr_pack(&l_hdr, (uint8_t *)l_msg, DAP_GOSSIP_MSG_HDR_WIRE_SIZE) != 0) {
        pthread_rwlock_unlock(&s_gossip_lock);
        DAP_DELETE(l_msg_item);
        log_it(L_ERROR, "Can't pack gossip message header");
        return;
    }
    *(dap_cluster_node_addr_t *)l_msg->trace_n_payload = g_node_addr;
    memcpy(l_msg->trace_n_payload + l_hdr.trace_len, a_payload, a_payload_size);
    dap_ht_add_by_hashvalue(s_gossip_last_msgs, payload_hash, sizeof(dap_hash_t), l_hash_value, l_msg_item);
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
        unsigned l_hash_value = dap_ht_hash_value(l_payload_hash, sizeof(dap_hash_t));
        pthread_rwlock_wrlock(&s_gossip_lock);
        dap_ht_find_by_hashvalue(s_gossip_last_msgs, l_ch_pkt->data, sizeof(dap_hash_t), l_hash_value, l_msg_item);
        if (l_msg_item) {
            if (l_msg_item->timestamp < dap_nanotime_now() - DAP_GOSSIP_LIFETIME * 1000000000UL) {
                debug_if(s_debug_more, L_INFO, "Packet for hash %s is derelict", dap_hash_fast_to_str_static(l_payload_hash));
                dap_ht_del(s_gossip_last_msgs, l_msg_item);
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
            dap_ht_add_by_hashvalue(s_gossip_last_msgs, payload_hash, sizeof(dap_hash_t), l_hash_value, l_item_new);
            debug_if(s_debug_more, L_INFO, "OUT: GOSSIP_REQUEST packet for hash %s", dap_hash_fast_to_str_static(l_payload_hash));
            // Send request for data associated with this hash
            dap_stream_ch_pkt_write_unsafe(a_ch, DAP_STREAM_CH_GOSSIP_MSG_TYPE_REQUEST, l_payload_hash, sizeof(dap_hash_t));
        }
        pthread_rwlock_unlock(&s_gossip_lock);
    } break;

    case DAP_STREAM_CH_GOSSIP_MSG_TYPE_DATA: {
        if (l_ch_pkt->hdr.data_size < DAP_GOSSIP_MSG_HDR_WIRE_SIZE) {
            log_it(L_WARNING, "Incorrect gossip message data size %u, must be at least %u",
                                                l_ch_pkt->hdr.data_size, DAP_GOSSIP_MSG_HDR_WIRE_SIZE);
            return false;
        }
        dap_gossip_msg_mem_t l_hdr;
        if (dap_gossip_msg_hdr_unpack((const uint8_t *)l_ch_pkt->data, l_ch_pkt->hdr.data_size, &l_hdr) != 0) {
            log_it(L_WARNING, "Invalid gossip message header");
            return false;
        }
        if (l_ch_pkt->hdr.data_size != dap_gossip_msg_get_size((dap_gossip_msg_t *)&l_hdr)) {
            log_it(L_WARNING, "Incorrect gossip message data size %u, expected %" DAP_UINT64_FORMAT_U,
                                                l_ch_pkt->hdr.data_size, (uint64_t)dap_gossip_msg_get_size((dap_gossip_msg_t *)&l_hdr));
            return false;
        }
        if (l_hdr.version != DAP_GOSSIP_CURRENT_VERSION) {
            log_it(L_ERROR, "Incorrect gossip protocol version %hhu, current version is %u",
                                                         l_hdr.version, DAP_GOSSIP_CURRENT_VERSION);
            return false;
        }
        if (l_hdr.trace_len % sizeof(dap_cluster_node_addr_t) != 0) {
            log_it(L_WARNING, "Unaligned gossip message tracepath size %u", l_hdr.trace_len);
            return false;
        }
        if (!l_hdr.payload_len) {
            log_it(L_WARNING, "Zero size of gossip message payload");
            return false;
        }
        debug_if(s_debug_more, L_INFO, "IN: GOSSIP_DATA packet for hash %s", dap_hash_fast_to_str_static((dap_hash_fast_t *)&l_hdr.payload_hash));
        unsigned l_hash_value = dap_ht_hash_value(&l_hdr.payload_hash, sizeof(dap_hash_t));
        struct gossip_msg_item *l_payload_item = NULL, *l_payload_item_new;
        pthread_rwlock_wrlock(&s_gossip_lock);
        dap_ht_find_by_hashvalue(s_gossip_last_msgs, &l_hdr.payload_hash, sizeof(dap_hash_t), l_hash_value, l_payload_item);
        if (!l_payload_item || l_payload_item->with_payload) {
            // Get data for non requested hash or double data. Drop it
            pthread_rwlock_unlock(&s_gossip_lock);
            break;
        }
        if (l_payload_item->timestamp < dap_nanotime_now() - DAP_GOSSIP_LIFETIME * 1000000000UL) {
            dap_ht_del(s_gossip_last_msgs, l_payload_item);
            DAP_DELETE(l_payload_item);
            pthread_rwlock_unlock(&s_gossip_lock);
            break;
        }
        dap_cluster_t *l_links_cluster = dap_cluster_find(l_hdr.cluster_id);
        if (l_links_cluster) {
            dap_cluster_member_t *l_check = dap_cluster_member_find_unsafe(l_links_cluster, &a_ch->stream->node);
            if (!l_check) {
                log_it(L_WARNING, "Node with addr "NODE_ADDR_FP_STR" isn't a member of cluster %s",
                                            NODE_ADDR_FP_ARGS_S(a_ch->stream->node), l_links_cluster->mnemonim);
                dap_cluster_node_addr_t l_member = dap_cluster_get_random_link(l_links_cluster);
                if (dap_cluster_node_addr_is_blank(&l_member)) {
                    log_it(L_ERROR, "Cluster %s has no active members", l_links_cluster->mnemonim);
                    pthread_rwlock_unlock(&s_gossip_lock);
                    break;
                }
                debug_if(s_debug_more, L_INFO, "OUT: GOSSIP_REQUEST packet for hash %s", dap_hash_fast_to_str_static((dap_hash_fast_t *)&l_hdr.payload_hash));
                // Send request for data associated with this hash to another link
                dap_stream_ch_pkt_send_by_addr(&l_member, DAP_STREAM_CH_GOSSIP_ID, DAP_STREAM_CH_GOSSIP_MSG_TYPE_REQUEST,
                                               l_ch_pkt->data, sizeof(dap_hash_t));
                pthread_rwlock_unlock(&s_gossip_lock);
                break;
            }
        } else if (!IS_ZERO_128(l_hdr.cluster_id.raw)) {
            const char *l_guuid_str = dap_guuid_to_hex_str(l_hdr.cluster_id);
            log_it(L_ERROR, "Can't find cluster with ID %s for gossip message broadcasting", l_guuid_str);
            pthread_rwlock_unlock(&s_gossip_lock);
            break;
        }
        size_t l_payload_item_size = dap_gossip_msg_get_size((dap_gossip_msg_t *)&l_hdr) + sizeof(g_node_addr) + sizeof(struct gossip_msg_item);
        dap_ht_del(s_gossip_last_msgs, l_payload_item);
        l_payload_item_new = DAP_REALLOC(l_payload_item, l_payload_item_size);
        if (!l_payload_item_new) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            pthread_rwlock_unlock(&s_gossip_lock);
            break;
        }
        l_payload_item = l_payload_item_new;
        dap_ht_add_by_hashvalue(s_gossip_last_msgs, payload_hash, sizeof(dap_hash_t), l_hash_value, l_payload_item);
        l_payload_item->with_payload = true;
        // Copy message and append g_node_addr to pathtrace
        const uint32_t l_old_trace = l_hdr.trace_len;
        dap_gossip_msg_mem_t l_new_hdr = l_hdr;
        l_new_hdr.trace_len = l_old_trace + (uint32_t)sizeof(g_node_addr);
        dap_gossip_msg_t *l_msg_new = (dap_gossip_msg_t *)l_payload_item->message;
        if (dap_gossip_msg_hdr_pack(&l_new_hdr, (uint8_t *)l_msg_new, DAP_GOSSIP_MSG_HDR_WIRE_SIZE) != 0) {
            log_it(L_ERROR, "Can't pack gossip message header for relay");
            pthread_rwlock_unlock(&s_gossip_lock);
            break;
        }
        memcpy(l_msg_new->trace_n_payload, l_ch_pkt->data + DAP_GOSSIP_MSG_HDR_WIRE_SIZE, l_old_trace);
        *(dap_cluster_node_addr_t *)(l_msg_new->trace_n_payload + l_old_trace) = g_node_addr;
        memcpy(l_msg_new->trace_n_payload + l_new_hdr.trace_len, l_ch_pkt->data + DAP_GOSSIP_MSG_HDR_WIRE_SIZE + l_old_trace, l_hdr.payload_len);
        // Broadcast new message
        debug_if(s_debug_more, L_INFO, "OUT: GOSSIP_HASH broadcast for hash %s",
                                        dap_hash_fast_to_str_static((dap_hash_fast_t *)&l_new_hdr.payload_hash));
        dap_cluster_broadcast(l_links_cluster, DAP_STREAM_CH_GOSSIP_ID,
                              DAP_STREAM_CH_GOSSIP_MSG_TYPE_HASH,
                              &l_new_hdr.payload_hash, sizeof(dap_hash_t),
                              (dap_cluster_node_addr_t *)l_msg_new->trace_n_payload,
                              l_new_hdr.trace_len / sizeof(dap_cluster_node_addr_t));
        pthread_rwlock_unlock(&s_gossip_lock);
        // Call back the payload func if any
        struct gossip_callback *l_callback = s_get_callbacks_by_ch_id((char)l_hdr.payload_ch_id);
        if (!l_callback) {
            log_it(L_ERROR, "Can't find channel callback for channel '%c' to gossip message apply", (char)l_hdr.payload_ch_id);
            break;
        }
        assert(l_callback->callback_payload);
        l_callback->callback_payload(l_msg_new->trace_n_payload + l_new_hdr.trace_len, l_hdr.payload_len, a_ch->stream->node);
    } break;

    default:
        log_it(L_WARNING, "Unknown gossip packet type %hhu", l_ch_pkt->hdr.type);
        return false;
    }

    return true;
}

