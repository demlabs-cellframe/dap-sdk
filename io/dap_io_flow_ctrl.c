/*
 * Authors:
 * Dmitriy Gerasimov <dmitriy.gerasimov@demlabs.net>
 * DeM Labs Ltd   https://demlabs.net
 * Copyright (c) 2024-2026
 * All rights reserved.
 *
 * DAP SDK Flow Control for Datagram Protocols
 * 
 * Provides reliable delivery over unreliable datagram protocols (UDP, SCTP):
 * - Packet tracking & retransmission (ACK-based reliability)
 * - Sequence tracking & reordering (out-of-order delivery)
 * - Keep-alive mechanism (connection liveness)
 */

#include <string.h>
#include <pthread.h>
#include "dap_common.h"
#include "dap_time.h"
#include "dap_io_flow_ctrl.h"
#include "dap_io_flow.h"
#include "dap_timerfd.h"
#include "dap_worker.h"

#define LOG_TAG "dap_io_flow_ctrl"

// Internal structures

typedef struct send_window_entry {
    void *packet;                   // Full packet (allocated by prepare callback)
    size_t packet_size;
    uint64_t seq_num;
    uint64_t timestamp_ns;          // When sent
    uint32_t retransmit_count;
    bool acked;
} send_window_entry_t;

typedef struct recv_window_entry {
    uint8_t *payload;               // Payload data (copied)
    size_t payload_size;
    uint64_t seq_num;
    bool received;
} recv_window_entry_t;

struct dap_io_flow_ctrl {
    dap_io_flow_t *flow;            // Associated flow
    dap_io_flow_ctrl_flags_t flags; // Active flags
    dap_io_flow_ctrl_config_t config; // Configuration
    dap_io_flow_ctrl_callbacks_t callbacks; // Transport callbacks
    
    // Send window (retransmission)
    send_window_entry_t *send_window;
    size_t send_window_size;
    uint64_t send_seq_next;         // Next sequence to send
    uint64_t send_seq_acked;        // Highest acked sequence
    pthread_mutex_t send_mutex;
    
    // Receive window (reordering)
    recv_window_entry_t *recv_window;
    size_t recv_window_size;
    uint64_t recv_seq_expected;     // Next expected in-order sequence
    uint64_t recv_seq_highest;      // Highest received (may be out-of-order)
    pthread_mutex_t recv_mutex;
    
    // Timers
    dap_timerfd_t *retransmit_timer;
    dap_timerfd_t *keepalive_timer;
    uint64_t last_activity_ns;      // Last packet send/recv time
    
    // Statistics
    _Atomic uint64_t stats_sent;
    _Atomic uint64_t stats_retransmitted;
    _Atomic uint64_t stats_recv;
    _Atomic uint64_t stats_out_of_order;
    _Atomic uint64_t stats_duplicate;
    _Atomic uint64_t stats_lost;
};

// Global state
static bool s_inited = false;

// Forward declarations
static bool s_retransmit_timer_callback(void *a_arg);
static bool s_keepalive_timer_callback(void *a_arg);

//===================================================================
// PUBLIC API
//===================================================================

/**
 * @brief Initialize flow control subsystem
 */
int dap_io_flow_ctrl_init(void)
{
    if (s_inited) {
        return 0;
    }
    
    log_it(L_NOTICE, "Flow Control subsystem initialized");
    s_inited = true;
    return 0;
}

/**
 * @brief Deinitialize flow control subsystem
 */
void dap_io_flow_ctrl_deinit(void)
{
    if (!s_inited) {
        return;
    }
    
    log_it(L_NOTICE, "Flow Control subsystem deinitialized");
    s_inited = false;
}

/**
 * @brief Create flow control for a flow
 */
dap_io_flow_ctrl_t* dap_io_flow_ctrl_create(
    dap_io_flow_t *a_flow,
    dap_io_flow_ctrl_flags_t a_flags,
    const dap_io_flow_ctrl_config_t *a_config,
    const dap_io_flow_ctrl_callbacks_t *a_callbacks)
{
    if (!a_flow || !a_callbacks) {
        log_it(L_ERROR, "Invalid arguments for flow control creation");
        return NULL;
    }
    
    // Validate callbacks
    if (!a_callbacks->packet_prepare || !a_callbacks->packet_parse ||
        !a_callbacks->packet_send || !a_callbacks->packet_free ||
        !a_callbacks->payload_deliver) {
        log_it(L_ERROR, "Missing required callbacks");
        return NULL;
    }
    
    dap_io_flow_ctrl_t *l_ctrl = DAP_NEW_Z(dap_io_flow_ctrl_t);
    if (!l_ctrl) {
        log_it(L_ERROR, "Failed to allocate flow control");
        return NULL;
    }
    
    l_ctrl->flow = a_flow;
    l_ctrl->flags = a_flags;
    l_ctrl->callbacks = *a_callbacks;
    
    // Use provided config or defaults
    if (a_config) {
        l_ctrl->config = *a_config;
    } else {
        dap_io_flow_ctrl_get_default_config(&l_ctrl->config);
    }
    
    // Initialize send window if retransmission enabled
    if (a_flags & DAP_IO_FLOW_CTRL_RETRANSMIT) {
        l_ctrl->send_window_size = l_ctrl->config.send_window_size;
        l_ctrl->send_window = DAP_NEW_Z_COUNT(send_window_entry_t, l_ctrl->send_window_size);
        if (!l_ctrl->send_window) {
            log_it(L_ERROR, "Failed to allocate send window");
            DAP_DELETE(l_ctrl);
            return NULL;
        }
        pthread_mutex_init(&l_ctrl->send_mutex, NULL);
        l_ctrl->send_seq_next = 1;  // Start from 1 (0 = invalid)
        l_ctrl->send_seq_acked = 0;
    }
    
    // Initialize receive window if reordering enabled
    if (a_flags & DAP_IO_FLOW_CTRL_REORDER) {
        l_ctrl->recv_window_size = l_ctrl->config.recv_window_size;
        l_ctrl->recv_window = DAP_NEW_Z_COUNT(recv_window_entry_t, l_ctrl->recv_window_size);
        if (!l_ctrl->recv_window) {
            log_it(L_ERROR, "Failed to allocate receive window");
            if (l_ctrl->send_window) {
                pthread_mutex_destroy(&l_ctrl->send_mutex);
                DAP_DELETE(l_ctrl->send_window);
            }
            DAP_DELETE(l_ctrl);
            return NULL;
        }
        pthread_mutex_init(&l_ctrl->recv_mutex, NULL);
        l_ctrl->recv_seq_expected = 1;  // Expect first packet
        l_ctrl->recv_seq_highest = 0;
    }
    
    // Initialize statistics
    atomic_init(&l_ctrl->stats_sent, 0);
    atomic_init(&l_ctrl->stats_retransmitted, 0);
    atomic_init(&l_ctrl->stats_recv, 0);
    atomic_init(&l_ctrl->stats_out_of_order, 0);
    atomic_init(&l_ctrl->stats_duplicate, 0);
    atomic_init(&l_ctrl->stats_lost, 0);
    
    l_ctrl->last_activity_ns = dap_nanotime_now();
    
    // Start retransmission timer if enabled
    if (a_flags & DAP_IO_FLOW_CTRL_RETRANSMIT) {
        dap_worker_t *l_worker = dap_worker_get_current();
        if (l_worker) {
            l_ctrl->retransmit_timer = dap_timerfd_start_on_worker(
                l_worker,
                l_ctrl->config.retransmit_timeout_ms / 2,  // Check twice per timeout period
                s_retransmit_timer_callback,
                l_ctrl
            );
            if (!l_ctrl->retransmit_timer) {
                log_it(L_WARNING, "Failed to start retransmission timer");
            }
        }
    }
    
    // Start keep-alive timer if enabled
    if (a_flags & DAP_IO_FLOW_CTRL_KEEPALIVE) {
        dap_worker_t *l_worker = dap_worker_get_current();
        if (l_worker) {
            l_ctrl->keepalive_timer = dap_timerfd_start_on_worker(
                l_worker,
                l_ctrl->config.keepalive_interval_ms / 2,  // Check twice per interval
                s_keepalive_timer_callback,
                l_ctrl
            );
            if (!l_ctrl->keepalive_timer) {
                log_it(L_WARNING, "Failed to start keep-alive timer");
            }
        }
    }
    
    log_it(L_DEBUG, "Flow Control created: flags=0x%02x, send_window=%zu, recv_window=%zu",
           a_flags, l_ctrl->send_window_size, l_ctrl->recv_window_size);
    
    return l_ctrl;
}

/**
 * @brief Destroy flow control
 */
void dap_io_flow_ctrl_delete(dap_io_flow_ctrl_t *a_ctrl)
{
    if (!a_ctrl) {
        return;
    }
    
    // Stop timers
    if (a_ctrl->retransmit_timer) {
        dap_timerfd_delete_unsafe(a_ctrl->retransmit_timer);
        a_ctrl->retransmit_timer = NULL;
    }
    if (a_ctrl->keepalive_timer) {
        dap_timerfd_delete_unsafe(a_ctrl->keepalive_timer);
        a_ctrl->keepalive_timer = NULL;
    }
    
    // Clean send window
    if (a_ctrl->send_window) {
        for (size_t i = 0; i < a_ctrl->send_window_size; i++) {
            if (a_ctrl->send_window[i].packet) {
                a_ctrl->callbacks.packet_free(a_ctrl->send_window[i].packet, a_ctrl->callbacks.arg);
            }
        }
        pthread_mutex_destroy(&a_ctrl->send_mutex);
        DAP_DEL_Z(a_ctrl->send_window);
    }
    
    // Clean receive window
    if (a_ctrl->recv_window) {
        for (size_t i = 0; i < a_ctrl->recv_window_size; i++) {
            DAP_DEL_Z(a_ctrl->recv_window[i].payload);
        }
        pthread_mutex_destroy(&a_ctrl->recv_mutex);
        DAP_DEL_Z(a_ctrl->recv_window);
    }
    
    log_it(L_DEBUG, "Flow Control deleted: sent=%lu, retrans=%lu, recv=%lu, lost=%lu",
           atomic_load(&a_ctrl->stats_sent), atomic_load(&a_ctrl->stats_retransmitted),
           atomic_load(&a_ctrl->stats_recv), atomic_load(&a_ctrl->stats_lost));
    
    DAP_DELETE(a_ctrl);
}

/**
 * @brief Get default configuration
 */
void dap_io_flow_ctrl_get_default_config(dap_io_flow_ctrl_config_t *a_config)
{
    if (!a_config) {
        return;
    }
    
    a_config->retransmit_timeout_ms = 200;
    a_config->max_retransmit_count = 5;
    a_config->send_window_size = 64;
    a_config->recv_window_size = 128;
    a_config->max_out_of_order_delay_ms = 1000;
    a_config->keepalive_interval_ms = 5000;
    a_config->keepalive_timeout_ms = 15000;
}

/**
 * @brief Set flow control flags (dynamic enable/disable)
 */
int dap_io_flow_ctrl_set_flags(dap_io_flow_ctrl_t *a_ctrl, dap_io_flow_ctrl_flags_t a_flags)
{
    if (!a_ctrl) {
        return -1;
    }
    
    // TODO: Implement dynamic flag changes
    log_it(L_WARNING, "Dynamic flag changes not yet implemented");
    a_ctrl->flags = a_flags;
    
    return 0;
}

/**
 * @brief Get current flow control flags
 */
dap_io_flow_ctrl_flags_t dap_io_flow_ctrl_get_flags(dap_io_flow_ctrl_t *a_ctrl)
{
    return a_ctrl ? a_ctrl->flags : 0;
}

/**
 * @brief Send payload with flow control
 */
int dap_io_flow_ctrl_send(dap_io_flow_ctrl_t *a_ctrl, const void *a_payload, size_t a_payload_size)
{
    if (!a_ctrl || !a_payload || a_payload_size == 0) {
        return -1;
    }
    
    // Assign sequence number
    uint64_t l_seq_num = 0;
    if (a_ctrl->flags & DAP_IO_FLOW_CTRL_RETRANSMIT) {
        pthread_mutex_lock(&a_ctrl->send_mutex);
        l_seq_num = a_ctrl->send_seq_next++;
        pthread_mutex_unlock(&a_ctrl->send_mutex);
    }
    
    // Prepare metadata
    dap_io_flow_pkt_metadata_t l_metadata = {
        .seq_num = l_seq_num,
        .ack_seq = (a_ctrl->flags & DAP_IO_FLOW_CTRL_REORDER) ? a_ctrl->recv_seq_expected - 1 : 0,
        .timestamp_ms = (uint32_t)(dap_nanotime_now() / 1000000),
        .is_keepalive = false,
        .is_retransmit = false,
    };
    
    // Prepare packet (add header)
    void *l_packet = NULL;
    size_t l_packet_size = 0;
    int l_ret = a_ctrl->callbacks.packet_prepare(a_ctrl->flow, &l_metadata, a_payload, a_payload_size,
                                                  &l_packet, &l_packet_size, a_ctrl->callbacks.arg);
    if (l_ret != 0 || !l_packet) {
        log_it(L_ERROR, "Failed to prepare packet: ret=%d", l_ret);
        return -2;
    }
    
    // Send packet
    l_ret = a_ctrl->callbacks.packet_send(a_ctrl->flow, l_packet, l_packet_size, a_ctrl->callbacks.arg);
    if (l_ret != 0) {
        log_it(L_WARNING, "Failed to send packet: ret=%d", l_ret);
        a_ctrl->callbacks.packet_free(l_packet, a_ctrl->callbacks.arg);
        return -3;
    }
    
    // Track for retransmission if enabled
    if (a_ctrl->flags & DAP_IO_FLOW_CTRL_RETRANSMIT) {
        pthread_mutex_lock(&a_ctrl->send_mutex);
        size_t l_idx = (l_seq_num - 1) % a_ctrl->send_window_size;
        
        // Free old packet if slot occupied
        if (a_ctrl->send_window[l_idx].packet) {
            a_ctrl->callbacks.packet_free(a_ctrl->send_window[l_idx].packet, a_ctrl->callbacks.arg);
        }
        
        a_ctrl->send_window[l_idx].packet = l_packet;
        a_ctrl->send_window[l_idx].packet_size = l_packet_size;
        a_ctrl->send_window[l_idx].seq_num = l_seq_num;
        a_ctrl->send_window[l_idx].timestamp_ns = dap_nanotime_now();
        a_ctrl->send_window[l_idx].retransmit_count = 0;
        a_ctrl->send_window[l_idx].acked = false;
        
        pthread_mutex_unlock(&a_ctrl->send_mutex);
    } else {
        // No retransmission tracking - free immediately
        a_ctrl->callbacks.packet_free(l_packet, a_ctrl->callbacks.arg);
    }
    
    atomic_fetch_add(&a_ctrl->stats_sent, 1);
    a_ctrl->last_activity_ns = dap_nanotime_now();
    
    return 0;
}

/**
 * @brief Process received packet
 */
int dap_io_flow_ctrl_recv(dap_io_flow_ctrl_t *a_ctrl, const void *a_packet, size_t a_packet_size)
{
    if (!a_ctrl || !a_packet || a_packet_size == 0) {
        return -1;
    }
    
    // Parse packet
    dap_io_flow_pkt_metadata_t l_metadata = {0};
    const void *l_payload = NULL;
    size_t l_payload_size = 0;
    
    int l_ret = a_ctrl->callbacks.packet_parse(a_ctrl->flow, a_packet, a_packet_size,
                                                &l_metadata, &l_payload, &l_payload_size,
                                                a_ctrl->callbacks.arg);
    if (l_ret != 0) {
        log_it(L_WARNING, "Failed to parse packet: ret=%d", l_ret);
        return -2;
    }
    
    a_ctrl->last_activity_ns = dap_nanotime_now();
    
    // Process ACK if retransmission enabled
    if ((a_ctrl->flags & DAP_IO_FLOW_CTRL_RETRANSMIT) && l_metadata.ack_seq > 0) {
        pthread_mutex_lock(&a_ctrl->send_mutex);
        
        // Mark all packets up to ack_seq as acknowledged
        for (uint64_t seq = a_ctrl->send_seq_acked + 1; seq <= l_metadata.ack_seq; seq++) {
            size_t l_idx = (seq - 1) % a_ctrl->send_window_size;
            if (a_ctrl->send_window[l_idx].seq_num == seq && !a_ctrl->send_window[l_idx].acked) {
                a_ctrl->send_window[l_idx].acked = true;
                // Free acknowledged packet
                if (a_ctrl->send_window[l_idx].packet) {
                    a_ctrl->callbacks.packet_free(a_ctrl->send_window[l_idx].packet, a_ctrl->callbacks.arg);
                    a_ctrl->send_window[l_idx].packet = NULL;
                }
            }
        }
        
        if (l_metadata.ack_seq > a_ctrl->send_seq_acked) {
            a_ctrl->send_seq_acked = l_metadata.ack_seq;
        }
        
        pthread_mutex_unlock(&a_ctrl->send_mutex);
    }
    
    // Handle keep-alive packet
    if (l_metadata.is_keepalive) {
        debug_if(true, L_DEBUG, "Received keep-alive packet");
        return 0;
    }
    
    // Deliver payload
    if (l_payload_size > 0) {
        if (a_ctrl->flags & DAP_IO_FLOW_CTRL_REORDER) {
            // REORDERING ENABLED: Buffer out-of-order packets
            pthread_mutex_lock(&a_ctrl->recv_mutex);
            
            uint64_t l_seq = l_metadata.seq_num;
            
            // Check if this is the expected sequence
            if (l_seq == a_ctrl->recv_seq_expected) {
                // IN-ORDER: Deliver immediately + check buffered packets
                a_ctrl->callbacks.payload_deliver(a_ctrl->flow, l_payload, l_payload_size, a_ctrl->callbacks.arg);
                a_ctrl->recv_seq_expected++;
                atomic_fetch_add(&a_ctrl->stats_recv, 1);
                
                // Check if we have buffered packets that can now be delivered
                while (true) {
                    size_t l_idx = (a_ctrl->recv_seq_expected - 1) % a_ctrl->recv_window_size;
                    if (a_ctrl->recv_window[l_idx].received && 
                        a_ctrl->recv_window[l_idx].seq_num == a_ctrl->recv_seq_expected) {
                        // Deliver buffered packet
                        a_ctrl->callbacks.payload_deliver(a_ctrl->flow, 
                                                          a_ctrl->recv_window[l_idx].payload,
                                                          a_ctrl->recv_window[l_idx].payload_size,
                                                          a_ctrl->callbacks.arg);
                        // Free buffered payload
                        DAP_DEL_Z(a_ctrl->recv_window[l_idx].payload);
                        a_ctrl->recv_window[l_idx].received = false;
                        a_ctrl->recv_seq_expected++;
                        atomic_fetch_add(&a_ctrl->stats_recv, 1);
                    } else {
                        break;  // No more consecutive packets
                    }
                }
            } else if (l_seq > a_ctrl->recv_seq_expected) {
                // OUT-OF-ORDER: Buffer for later delivery
                size_t l_idx = (l_seq - 1) % a_ctrl->recv_window_size;
                
                // Check if already received (duplicate)
                if (a_ctrl->recv_window[l_idx].received && 
                    a_ctrl->recv_window[l_idx].seq_num == l_seq) {
                    log_it(L_WARNING, "Duplicate packet: seq=%lu", l_seq);
                    atomic_fetch_add(&a_ctrl->stats_duplicate, 1);
                } else {
                    // Buffer packet
                    if (a_ctrl->recv_window[l_idx].payload) {
                        DAP_DELETE(a_ctrl->recv_window[l_idx].payload);
                    }
                    
                    a_ctrl->recv_window[l_idx].payload = DAP_NEW_SIZE(uint8_t, l_payload_size);
                    if (a_ctrl->recv_window[l_idx].payload) {
                        memcpy(a_ctrl->recv_window[l_idx].payload, l_payload, l_payload_size);
                        a_ctrl->recv_window[l_idx].payload_size = l_payload_size;
                        a_ctrl->recv_window[l_idx].seq_num = l_seq;
                        a_ctrl->recv_window[l_idx].received = true;
                        
                        if (l_seq > a_ctrl->recv_seq_highest) {
                            a_ctrl->recv_seq_highest = l_seq;
                        }
                        
                        log_it(L_DEBUG, "Buffered out-of-order packet: seq=%lu, expected=%lu", 
                               l_seq, a_ctrl->recv_seq_expected);
                        atomic_fetch_add(&a_ctrl->stats_out_of_order, 1);
                    } else {
                        log_it(L_ERROR, "Failed to allocate buffer for out-of-order packet");
                    }
                }
            } else {
                // DUPLICATE or REPLAY: seq < expected
                log_it(L_WARNING, "Old/duplicate packet: seq=%lu, expected=%lu", 
                       l_seq, a_ctrl->recv_seq_expected);
                atomic_fetch_add(&a_ctrl->stats_duplicate, 1);
            }
            
            pthread_mutex_unlock(&a_ctrl->recv_mutex);
        } else {
            // NO REORDERING: Deliver immediately
            a_ctrl->callbacks.payload_deliver(a_ctrl->flow, l_payload, l_payload_size, a_ctrl->callbacks.arg);
            atomic_fetch_add(&a_ctrl->stats_recv, 1);
        }
    }
    
    return 0;
}

/**
 * @brief Get statistics
 */
void dap_io_flow_ctrl_get_stats(
    dap_io_flow_ctrl_t *a_ctrl,
    uint64_t *a_sent_packets,
    uint64_t *a_retransmitted_packets,
    uint64_t *a_recv_packets,
    uint64_t *a_out_of_order_packets,
    uint64_t *a_duplicate_packets,
    uint64_t *a_lost_packets)
{
    if (!a_ctrl) {
        return;
    }
    
    if (a_sent_packets) *a_sent_packets = atomic_load(&a_ctrl->stats_sent);
    if (a_retransmitted_packets) *a_retransmitted_packets = atomic_load(&a_ctrl->stats_retransmitted);
    if (a_recv_packets) *a_recv_packets = atomic_load(&a_ctrl->stats_recv);
    if (a_out_of_order_packets) *a_out_of_order_packets = atomic_load(&a_ctrl->stats_out_of_order);
    if (a_duplicate_packets) *a_duplicate_packets = atomic_load(&a_ctrl->stats_duplicate);
    if (a_lost_packets) *a_lost_packets = atomic_load(&a_ctrl->stats_lost);
}

//===================================================================
// INTERNAL HELPERS
//===================================================================

/**
 * @brief Retransmission timer callback
 * 
 * Scans send window for unacked packets that timed out and retransmits them.
 */
static bool s_retransmit_timer_callback(void *a_arg)
{
    dap_io_flow_ctrl_t *l_ctrl = (dap_io_flow_ctrl_t *)a_arg;
    if (!l_ctrl) {
        return false;  // Stop timer
    }
    
    uint64_t l_now = dap_nanotime_now();
    uint64_t l_timeout_ns = l_ctrl->config.retransmit_timeout_ms * 1000000ULL;
    
    pthread_mutex_lock(&l_ctrl->send_mutex);
    
    // Scan send window for packets needing retransmission
    for (uint64_t seq = l_ctrl->send_seq_acked + 1; seq < l_ctrl->send_seq_next; seq++) {
        size_t l_idx = (seq - 1) % l_ctrl->send_window_size;
        send_window_entry_t *l_entry = &l_ctrl->send_window[l_idx];
        
        if (!l_entry->packet || l_entry->acked) {
            continue;  // Skip empty or acked slots
        }
        
        if (l_entry->seq_num != seq) {
            continue;  // Slot overwritten
        }
        
        // Check if timeout expired
        if (l_now - l_entry->timestamp_ns > l_timeout_ns) {
            // Check max retries
            if (l_entry->retransmit_count >= l_ctrl->config.max_retransmit_count) {
                log_it(L_WARNING, "Packet lost after %u retries: seq=%lu",
                       l_entry->retransmit_count, seq);
                atomic_fetch_add(&l_ctrl->stats_lost, 1);
                
                // Free lost packet
                l_ctrl->callbacks.packet_free(l_entry->packet, l_ctrl->callbacks.arg);
                l_entry->packet = NULL;
                l_entry->acked = true;  // Mark as done (lost)
                continue;
            }
            
            // RETRANSMIT
            int l_ret = l_ctrl->callbacks.packet_send(l_ctrl->flow, l_entry->packet, 
                                                       l_entry->packet_size, l_ctrl->callbacks.arg);
            if (l_ret == 0) {
                l_entry->timestamp_ns = l_now;
                l_entry->retransmit_count++;
                atomic_fetch_add(&l_ctrl->stats_retransmitted, 1);
                
                log_it(L_DEBUG, "Retransmitted packet: seq=%lu, retry=%u",
                       seq, l_entry->retransmit_count);
            } else {
                log_it(L_WARNING, "Failed to retransmit packet: seq=%lu, ret=%d", seq, l_ret);
            }
        }
    }
    
    pthread_mutex_unlock(&l_ctrl->send_mutex);
    
    return true;  // Continue timer
}

/**
 * @brief Keep-alive timer callback
 * 
 * Sends keep-alive packet if no activity, or detects timeout.
 */
static bool s_keepalive_timer_callback(void *a_arg)
{
    dap_io_flow_ctrl_t *l_ctrl = (dap_io_flow_ctrl_t *)a_arg;
    if (!l_ctrl) {
        return false;  // Stop timer
    }
    
    uint64_t l_now = dap_nanotime_now();
    uint64_t l_silence_ns = l_now - l_ctrl->last_activity_ns;
    uint64_t l_timeout_ns = l_ctrl->config.keepalive_timeout_ms * 1000000ULL;
    
    // Check if connection timed out
    if (l_silence_ns > l_timeout_ns) {
        log_it(L_WARNING, "Keep-alive timeout: %lu ms silence",
               l_silence_ns / 1000000);
        
        // Call timeout callback
        if (l_ctrl->callbacks.keepalive_timeout) {
            l_ctrl->callbacks.keepalive_timeout(l_ctrl->flow, l_ctrl->callbacks.arg);
        }
        
        return true;  // Continue timer (let upper layer decide to close)
    }
    
    // Check if we need to send keep-alive
    uint64_t l_interval_ns = l_ctrl->config.keepalive_interval_ms * 1000000ULL;
    if (l_silence_ns > l_interval_ns) {
        // Send keep-alive packet
        dap_io_flow_pkt_metadata_t l_metadata = {
            .seq_num = 0,  // Keep-alive has no sequence
            .ack_seq = (l_ctrl->flags & DAP_IO_FLOW_CTRL_REORDER) ? 
                       l_ctrl->recv_seq_expected - 1 : 0,
            .timestamp_ms = (uint32_t)(l_now / 1000000),
            .is_keepalive = true,
            .is_retransmit = false,
        };
        
        void *l_packet = NULL;
        size_t l_packet_size = 0;
        int l_ret = l_ctrl->callbacks.packet_prepare(l_ctrl->flow, &l_metadata, NULL, 0,
                                                      &l_packet, &l_packet_size, 
                                                      l_ctrl->callbacks.arg);
        if (l_ret == 0 && l_packet) {
            l_ret = l_ctrl->callbacks.packet_send(l_ctrl->flow, l_packet, l_packet_size,
                                                   l_ctrl->callbacks.arg);
            l_ctrl->callbacks.packet_free(l_packet, l_ctrl->callbacks.arg);
            
            if (l_ret == 0) {
                l_ctrl->last_activity_ns = l_now;
                log_it(L_DEBUG, "Sent keep-alive packet");
            }
        }
    }
    
    return true;  // Continue timer
}

