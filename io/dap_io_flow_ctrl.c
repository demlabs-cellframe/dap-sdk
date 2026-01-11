/*
 * Authors:
 * Dmitriy Gerasimov <dmitriy.gerasimov@demlabs.net>
 * DeM Labs Ltd   https://demlabs.net
 * Copyright (c) 2024
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

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "dap_io_flow_ctrl.h"
#include "dap_io_flow.h"
#include "dap_common.h"
#include "dap_timerfd.h"
#include "dap_worker.h"

#define LOG_TAG "dap_io_flow_ctrl"

// Default configuration values
#define DEFAULT_RETRANSMIT_TIMEOUT_MS       200
#define DEFAULT_MAX_RETRANSMIT_COUNT        5
#define DEFAULT_SEND_WINDOW_SIZE            64
#define DEFAULT_RECV_WINDOW_SIZE            128
#define DEFAULT_MAX_OUT_OF_ORDER_DELAY_MS   1000
#define DEFAULT_KEEPALIVE_INTERVAL_MS       5000
#define DEFAULT_KEEPALIVE_TIMEOUT_MS        15000

/**
 * @brief Tracked packet in send window
 */
typedef struct dap_io_flow_ctrl_tracked_pkt {
    uint64_t seq_num;                    // Sequence number
    void *packet;                        // Full packet (header + payload)
    size_t packet_size;                  // Full packet size
    uint64_t send_time_ms;              // When sent (for timeout)
    uint32_t retransmit_count;          // How many times retransmitted
    bool acked;                          // ACK received?
} dap_io_flow_ctrl_tracked_pkt_t;

/**
 * @brief Send window for retransmission tracking
 */
typedef struct dap_io_flow_ctrl_send_window {
    dap_io_flow_ctrl_tracked_pkt_t *packets;  // Circular buffer
    size_t capacity;                           // Window size
    size_t count;                              // Current packet count
    uint64_t next_seq_num;                     // Next sequence to assign
    uint64_t oldest_unacked_seq;               // First unacked packet
    pthread_mutex_t lock;
} dap_io_flow_ctrl_send_window_t;

/**
 * @brief Buffered packet in receive window
 */
typedef struct dap_io_flow_ctrl_buffered_pkt {
    uint64_t seq_num;                    // Sequence number
    void *payload;                       // Payload data (copy)
    size_t payload_size;                 // Payload size
    uint64_t recv_time_ms;              // When buffered (for timeout)
    bool valid;                          // Slot is occupied?
} dap_io_flow_ctrl_buffered_pkt_t;

/**
 * @brief Receive window for reordering
 */
typedef struct dap_io_flow_ctrl_recv_window {
    dap_io_flow_ctrl_buffered_pkt_t *packets;  // Out-of-order buffer
    size_t capacity;                            // Buffer size
    size_t count;                               // Current buffered count
    uint64_t next_expected_seq;                 // Next in-order sequence
    uint64_t highest_received_seq;              // Highest seen
    pthread_mutex_t lock;
} dap_io_flow_ctrl_recv_window_t;

/**
 * @brief Keep-alive state
 */
typedef struct dap_io_flow_ctrl_keepalive {
    dap_timerfd_t *timer;                // Timer for sending keep-alives
    uint64_t last_activity_ms;          // Last packet received (any packet)
    uint32_t interval_ms;               // Send interval
    uint32_t timeout_ms;                // Consider dead after this
} dap_io_flow_ctrl_keepalive_t;

/**
 * @brief Statistics
 */
typedef struct dap_io_flow_ctrl_stats {
    uint64_t sent_packets;              // Total sent
    uint64_t retransmitted_packets;     // Retransmitted
    uint64_t recv_packets;              // Total received
    uint64_t out_of_order_packets;      // Buffered out-of-order
    uint64_t duplicate_packets;         // Duplicates dropped
    uint64_t lost_packets;              // Lost (max retries exceeded)
} dap_io_flow_ctrl_stats_t;

/**
 * @brief Flow control context
 */
struct dap_io_flow_ctrl {
    dap_io_flow_t *flow;                        // Parent flow
    dap_io_flow_ctrl_flags_t flags;             // Active flags
    dap_io_flow_ctrl_config_t config;           // Configuration
    dap_io_flow_ctrl_callbacks_t callbacks;     // Transport callbacks
    
    // Mechanisms (allocated only if enabled)
    dap_io_flow_ctrl_send_window_t *send_window;   // NULL if retransmit disabled
    dap_io_flow_ctrl_recv_window_t *recv_window;   // NULL if reorder disabled
    dap_io_flow_ctrl_keepalive_t *keepalive;       // NULL if keepalive disabled
    
    dap_timerfd_t *retransmit_timer;            // Retransmission check timer
    
    dap_io_flow_ctrl_stats_t stats;             // Statistics
    pthread_mutex_t stats_lock;                 // Stats mutex
};

// Global state
static bool s_initialized = false;

// Helper functions
static uint64_t s_get_time_ms(void);
static int s_send_window_create(dap_io_flow_ctrl_t *a_ctrl);
static void s_send_window_destroy(dap_io_flow_ctrl_t *a_ctrl);
static int s_send_window_track(dap_io_flow_ctrl_t *a_ctrl, uint64_t a_seq_num, 
                                void *a_packet, size_t a_packet_size);
static void s_send_window_ack(dap_io_flow_ctrl_t *a_ctrl, uint64_t a_ack_seq);
static int s_recv_window_create(dap_io_flow_ctrl_t *a_ctrl);
static void s_recv_window_destroy(dap_io_flow_ctrl_t *a_ctrl);
static int s_recv_window_buffer(dap_io_flow_ctrl_t *a_ctrl, uint64_t a_seq_num,
                                 const void *a_payload, size_t a_payload_size);
static int s_recv_window_deliver_inorder(dap_io_flow_ctrl_t *a_ctrl);
static int s_keepalive_create(dap_io_flow_ctrl_t *a_ctrl);
static void s_keepalive_destroy(dap_io_flow_ctrl_t *a_ctrl);
static bool s_retransmit_timer_callback(void *a_arg);
static bool s_keepalive_timer_callback(void *a_arg);

/**
 * @brief Get current time in milliseconds
 */
static uint64_t s_get_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

/**
 * @brief Initialize flow control subsystem
 */
int dap_io_flow_ctrl_init(void)
{
    if (s_initialized) {
        log_it(L_WARNING, "Flow control already initialized");
        return 0;
    }
    
    log_it(L_NOTICE, "Initializing flow control subsystem");
    s_initialized = true;
    return 0;
}

/**
 * @brief Deinitialize flow control subsystem
 */
void dap_io_flow_ctrl_deinit(void)
{
    if (!s_initialized) {
        return;
    }
    
    log_it(L_NOTICE, "Deinitializing flow control subsystem");
    s_initialized = false;
}

/**
 * @brief Get default configuration
 */
void dap_io_flow_ctrl_get_default_config(dap_io_flow_ctrl_config_t *a_config)
{
    if (!a_config) {
        return;
    }
    
    a_config->retransmit_timeout_ms = DEFAULT_RETRANSMIT_TIMEOUT_MS;
    a_config->max_retransmit_count = DEFAULT_MAX_RETRANSMIT_COUNT;
    a_config->send_window_size = DEFAULT_SEND_WINDOW_SIZE;
    a_config->recv_window_size = DEFAULT_RECV_WINDOW_SIZE;
    a_config->max_out_of_order_delay_ms = DEFAULT_MAX_OUT_OF_ORDER_DELAY_MS;
    a_config->keepalive_interval_ms = DEFAULT_KEEPALIVE_INTERVAL_MS;
    a_config->keepalive_timeout_ms = DEFAULT_KEEPALIVE_TIMEOUT_MS;
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
    if (!s_initialized) {
        log_it(L_ERROR, "Flow control not initialized");
        return NULL;
    }
    
    if (!a_flow || !a_callbacks) {
        log_it(L_ERROR, "Invalid arguments for flow control creation");
        return NULL;
    }
    
    // Validate required callbacks
    if (!a_callbacks->packet_prepare || !a_callbacks->packet_parse ||
        !a_callbacks->packet_send || !a_callbacks->packet_free ||
        !a_callbacks->payload_deliver) {
        log_it(L_ERROR, "Missing required callbacks");
        return NULL;
    }
    
    dap_io_flow_ctrl_t *l_ctrl = DAP_NEW_Z(dap_io_flow_ctrl_t);
    if (!l_ctrl) {
        log_it(L_CRITICAL, "Failed to allocate flow control");
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
    
    // Initialize stats lock
    pthread_mutex_init(&l_ctrl->stats_lock, NULL);
    
    // Create mechanisms based on flags
    if (a_flags & DAP_IO_FLOW_CTRL_RETRANSMIT) {
        if (s_send_window_create(l_ctrl) != 0) {
            log_it(L_ERROR, "Failed to create send window");
            dap_io_flow_ctrl_delete(l_ctrl);
            return NULL;
        }
        
        // Create retransmit timer (periodic check)
        dap_worker_t *l_worker = dap_worker_get_current();
        if (l_worker) {
            l_ctrl->retransmit_timer = dap_timerfd_start_on_worker(
                l_worker,
                l_ctrl->config.retransmit_timeout_ms / 2,  // Check twice per timeout
                s_retransmit_timer_callback,
                l_ctrl);
            
            if (!l_ctrl->retransmit_timer) {
                log_it(L_ERROR, "Failed to create retransmit timer");
                dap_io_flow_ctrl_delete(l_ctrl);
                return NULL;
            }
        }
    }
    
    if (a_flags & DAP_IO_FLOW_CTRL_REORDER) {
        if (s_recv_window_create(l_ctrl) != 0) {
            log_it(L_ERROR, "Failed to create recv window");
            dap_io_flow_ctrl_delete(l_ctrl);
            return NULL;
        }
    }
    
    if (a_flags & DAP_IO_FLOW_CTRL_KEEPALIVE) {
        if (s_keepalive_create(l_ctrl) != 0) {
            log_it(L_ERROR, "Failed to create keep-alive");
            dap_io_flow_ctrl_delete(l_ctrl);
            return NULL;
        }
    }
    
    log_it(L_INFO, "Created flow control: flags=0x%02x, send_window=%zu, recv_window=%zu",
           a_flags, l_ctrl->config.send_window_size, l_ctrl->config.recv_window_size);
    
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
    
    // Stop and delete timers first
    if (a_ctrl->retransmit_timer) {
        dap_timerfd_delete_mt(a_ctrl->retransmit_timer->events_socket->worker,
                             a_ctrl->retransmit_timer->events_socket->uuid);
        a_ctrl->retransmit_timer = NULL;
    }
    
    // Destroy mechanisms
    s_send_window_destroy(a_ctrl);
    s_recv_window_destroy(a_ctrl);
    s_keepalive_destroy(a_ctrl);
    
    // Destroy stats lock
    pthread_mutex_destroy(&a_ctrl->stats_lock);
    
    DAP_DELETE(a_ctrl);
}

/**
 * @brief Set flow control flags (dynamic)
 */
int dap_io_flow_ctrl_set_flags(dap_io_flow_ctrl_t *a_ctrl, dap_io_flow_ctrl_flags_t a_flags)
{
    if (!a_ctrl) {
        return -1;
    }
    
    dap_io_flow_ctrl_flags_t l_old_flags = a_ctrl->flags;
    
    // Check what changed
    bool l_retransmit_enabled = (a_flags & DAP_IO_FLOW_CTRL_RETRANSMIT) && 
                                !(l_old_flags & DAP_IO_FLOW_CTRL_RETRANSMIT);
    bool l_retransmit_disabled = !(a_flags & DAP_IO_FLOW_CTRL_RETRANSMIT) && 
                                 (l_old_flags & DAP_IO_FLOW_CTRL_RETRANSMIT);
    
    bool l_reorder_enabled = (a_flags & DAP_IO_FLOW_CTRL_REORDER) && 
                             !(l_old_flags & DAP_IO_FLOW_CTRL_REORDER);
    bool l_reorder_disabled = !(a_flags & DAP_IO_FLOW_CTRL_REORDER) && 
                              (l_old_flags & DAP_IO_FLOW_CTRL_REORDER);
    
    bool l_keepalive_enabled = (a_flags & DAP_IO_FLOW_CTRL_KEEPALIVE) && 
                               !(l_old_flags & DAP_IO_FLOW_CTRL_KEEPALIVE);
    bool l_keepalive_disabled = !(a_flags & DAP_IO_FLOW_CTRL_KEEPALIVE) && 
                                (l_old_flags & DAP_IO_FLOW_CTRL_KEEPALIVE);
    
    // Enable/disable retransmission
    if (l_retransmit_enabled) {
        if (s_send_window_create(a_ctrl) != 0) {
            return -2;
        }
    } else if (l_retransmit_disabled) {
        s_send_window_destroy(a_ctrl);
    }
    
    // Enable/disable reordering
    if (l_reorder_enabled) {
        if (s_recv_window_create(a_ctrl) != 0) {
            return -3;
        }
    } else if (l_reorder_disabled) {
        s_recv_window_destroy(a_ctrl);
    }
    
    // Enable/disable keep-alive
    if (l_keepalive_enabled) {
        if (s_keepalive_create(a_ctrl) != 0) {
            return -4;
        }
    } else if (l_keepalive_disabled) {
        s_keepalive_destroy(a_ctrl);
    }
    
    a_ctrl->flags = a_flags;
    
    log_it(L_INFO, "Flow control flags changed: 0x%02x -> 0x%02x", l_old_flags, a_flags);
    
    return 0;
}

/**
 * @brief Get current flow control flags
 */
dap_io_flow_ctrl_flags_t dap_io_flow_ctrl_get_flags(dap_io_flow_ctrl_t *a_ctrl)
{
    return a_ctrl ? a_ctrl->flags : DAP_IO_FLOW_CTRL_NONE;
}

/**
 * @brief Send payload with flow control
 */
int dap_io_flow_ctrl_send(dap_io_flow_ctrl_t *a_ctrl, const void *a_payload, size_t a_payload_size)
{
    if (!a_ctrl || !a_payload || a_payload_size == 0) {
        return -1;
    }
    
    // Prepare metadata
    dap_io_flow_pkt_metadata_t l_metadata = {0};
    
    // Assign sequence number if retransmission enabled
    if (a_ctrl->send_window) {
        pthread_mutex_lock(&a_ctrl->send_window->lock);
        l_metadata.seq_num = a_ctrl->send_window->next_seq_num++;
        
        // Piggyback ACK if reordering enabled
        if (a_ctrl->recv_window) {
            pthread_mutex_lock(&a_ctrl->recv_window->lock);
            l_metadata.ack_seq = a_ctrl->recv_window->next_expected_seq - 1;
            pthread_mutex_unlock(&a_ctrl->recv_window->lock);
        }
        
        pthread_mutex_unlock(&a_ctrl->send_window->lock);
    }
    
    l_metadata.timestamp_ms = s_get_time_ms();
    l_metadata.is_keepalive = false;
    l_metadata.is_retransmit = false;
    
    // Call transport to prepare packet (add header)
    void *l_packet = NULL;
    size_t l_packet_size = 0;
    
    int l_ret = a_ctrl->callbacks.packet_prepare(
        a_ctrl->flow,
        &l_metadata,
        a_payload,
        a_payload_size,
        &l_packet,
        &l_packet_size,
        a_ctrl->callbacks.arg);
    
    if (l_ret != 0 || !l_packet) {
        log_it(L_ERROR, "packet_prepare callback failed: %d", l_ret);
        return -2;
    }
    
    // Track packet if retransmission enabled
    if (a_ctrl->send_window) {
        l_ret = s_send_window_track(a_ctrl, l_metadata.seq_num, l_packet, l_packet_size);
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to track packet in send window");
            a_ctrl->callbacks.packet_free(l_packet, a_ctrl->callbacks.arg);
            return -3;
        }
    }
    
    // Send packet via transport
    l_ret = a_ctrl->callbacks.packet_send(a_ctrl->flow, l_packet, l_packet_size,
                                         a_ctrl->callbacks.arg);
    
    if (l_ret != 0) {
        log_it(L_ERROR, "packet_send callback failed: %d", l_ret);
        if (!a_ctrl->send_window) {
            // If not tracking, free immediately
            a_ctrl->callbacks.packet_free(l_packet, a_ctrl->callbacks.arg);
        }
        return -4;
    }
    
    // Update statistics
    pthread_mutex_lock(&a_ctrl->stats_lock);
    a_ctrl->stats.sent_packets++;
    pthread_mutex_unlock(&a_ctrl->stats_lock);
    
    // If not tracking (no retransmit), free packet now
    if (!a_ctrl->send_window) {
        a_ctrl->callbacks.packet_free(l_packet, a_ctrl->callbacks.arg);
    }
    
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
    
    // Parse packet via transport callback
    dap_io_flow_pkt_metadata_t l_metadata = {0};
    const void *l_payload = NULL;
    size_t l_payload_size = 0;
    
    int l_ret = a_ctrl->callbacks.packet_parse(
        a_ctrl->flow,
        a_packet,
        a_packet_size,
        &l_metadata,
        &l_payload,
        &l_payload_size,
        a_ctrl->callbacks.arg);
    
    if (l_ret != 0) {
        log_it(L_ERROR, "packet_parse callback failed: %d", l_ret);
        return -2;
    }
    
    // Update keep-alive activity
    if (a_ctrl->keepalive) {
        a_ctrl->keepalive->last_activity_ms = s_get_time_ms();
    }
    
    // Process ACK if present
    if (a_ctrl->send_window && l_metadata.ack_seq > 0) {
        s_send_window_ack(a_ctrl, l_metadata.ack_seq);
    }
    
    // Handle keep-alive packet
    if (l_metadata.is_keepalive) {
        log_it(L_DEBUG, "Received keep-alive packet");
        return 0;  // Don't deliver keep-alive to upper layer
    }
    
    // Update statistics
    pthread_mutex_lock(&a_ctrl->stats_lock);
    a_ctrl->stats.recv_packets++;
    pthread_mutex_unlock(&a_ctrl->stats_lock);
    
    // Handle reordering if enabled
    if (a_ctrl->recv_window) {
        pthread_mutex_lock(&a_ctrl->recv_window->lock);
        
        uint64_t l_expected = a_ctrl->recv_window->next_expected_seq;
        
        if (l_metadata.seq_num == l_expected) {
            // In-order packet, deliver immediately
            pthread_mutex_unlock(&a_ctrl->recv_window->lock);
            
            l_ret = a_ctrl->callbacks.payload_deliver(
                a_ctrl->flow,
                l_payload,
                l_payload_size,
                a_ctrl->callbacks.arg);
            
            if (l_ret != 0) {
                log_it(L_ERROR, "payload_deliver callback failed: %d", l_ret);
                return -3;
            }
            
            // Update next expected
            pthread_mutex_lock(&a_ctrl->recv_window->lock);
            a_ctrl->recv_window->next_expected_seq++;
            pthread_mutex_unlock(&a_ctrl->recv_window->lock);
            
            // Try to deliver buffered in-order packets
            s_recv_window_deliver_inorder(a_ctrl);
            
            return 0;  // Delivered
            
        } else if (l_metadata.seq_num > l_expected) {
            // Out-of-order, buffer it
            log_it(L_DEBUG, "Out-of-order packet: seq=%lu, expected=%lu",
                   l_metadata.seq_num, l_expected);
            
            pthread_mutex_unlock(&a_ctrl->recv_window->lock);
            
            l_ret = s_recv_window_buffer(a_ctrl, l_metadata.seq_num, l_payload, l_payload_size);
            
            pthread_mutex_lock(&a_ctrl->stats_lock);
            a_ctrl->stats.out_of_order_packets++;
            pthread_mutex_unlock(&a_ctrl->stats_lock);
            
            return 1;  // Buffered
            
        } else {
            // Duplicate (seq_num < expected), drop
            log_it(L_DEBUG, "Duplicate packet: seq=%lu, expected=%lu",
                   l_metadata.seq_num, l_expected);
            
            pthread_mutex_lock(&a_ctrl->stats_lock);
            a_ctrl->stats.duplicate_packets++;
            pthread_mutex_unlock(&a_ctrl->stats_lock);
            
            pthread_mutex_unlock(&a_ctrl->recv_window->lock);
            return 0;  // Dropped
        }
    } else {
        // No reordering, deliver immediately
        l_ret = a_ctrl->callbacks.payload_deliver(
            a_ctrl->flow,
            l_payload,
            l_payload_size,
            a_ctrl->callbacks.arg);
        
        if (l_ret != 0) {
            log_it(L_ERROR, "payload_deliver callback failed: %d", l_ret);
            return -3;
        }
        
        return 0;  // Delivered
    }
}

/**
 * @brief Update keep-alive activity
 */
void dap_io_flow_ctrl_keepalive_activity(dap_io_flow_ctrl_t *a_ctrl)
{
    if (!a_ctrl || !a_ctrl->keepalive) {
        return;
    }
    
    a_ctrl->keepalive->last_activity_ms = s_get_time_ms();
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
    
    pthread_mutex_lock(&a_ctrl->stats_lock);
    
    if (a_sent_packets) *a_sent_packets = a_ctrl->stats.sent_packets;
    if (a_retransmitted_packets) *a_retransmitted_packets = a_ctrl->stats.retransmitted_packets;
    if (a_recv_packets) *a_recv_packets = a_ctrl->stats.recv_packets;
    if (a_out_of_order_packets) *a_out_of_order_packets = a_ctrl->stats.out_of_order_packets;
    if (a_duplicate_packets) *a_duplicate_packets = a_ctrl->stats.duplicate_packets;
    if (a_lost_packets) *a_lost_packets = a_ctrl->stats.lost_packets;
    
    pthread_mutex_unlock(&a_ctrl->stats_lock);
}

// ============================================================================
// SEND WINDOW IMPLEMENTATION (Retransmission Tracking)
// ============================================================================

static int s_send_window_create(dap_io_flow_ctrl_t *a_ctrl)
{
    a_ctrl->send_window = DAP_NEW_Z(dap_io_flow_ctrl_send_window_t);
    if (!a_ctrl->send_window) {
        return -1;
    }
    
    a_ctrl->send_window->capacity = a_ctrl->config.send_window_size;
    a_ctrl->send_window->packets = DAP_NEW_Z_COUNT(dap_io_flow_ctrl_tracked_pkt_t, 
                                                     a_ctrl->send_window->capacity);
    if (!a_ctrl->send_window->packets) {
        DAP_DELETE(a_ctrl->send_window);
        a_ctrl->send_window = NULL;
        return -2;
    }
    
    pthread_mutex_init(&a_ctrl->send_window->lock, NULL);
    
    a_ctrl->send_window->next_seq_num = 1;  // Start from 1 (0 = invalid)
    a_ctrl->send_window->oldest_unacked_seq = 1;
    
    log_it(L_DEBUG, "Created send window: capacity=%zu", a_ctrl->send_window->capacity);
    
    return 0;
}

static void s_send_window_destroy(dap_io_flow_ctrl_t *a_ctrl)
{
    if (!a_ctrl->send_window) {
        return;
    }
    
    // Free all tracked packets
    for (size_t i = 0; i < a_ctrl->send_window->capacity; i++) {
        if (a_ctrl->send_window->packets[i].packet) {
            a_ctrl->callbacks.packet_free(a_ctrl->send_window->packets[i].packet,
                                         a_ctrl->callbacks.arg);
        }
    }
    
    pthread_mutex_destroy(&a_ctrl->send_window->lock);
    DAP_DEL_Z(a_ctrl->send_window->packets);
    DAP_DELETE(a_ctrl->send_window);
    a_ctrl->send_window = NULL;
}

static int s_send_window_track(dap_io_flow_ctrl_t *a_ctrl, uint64_t a_seq_num,
                                void *a_packet, size_t a_packet_size)
{
    pthread_mutex_lock(&a_ctrl->send_window->lock);
    
    // Check if window is full
    if (a_ctrl->send_window->count >= a_ctrl->send_window->capacity) {
        pthread_mutex_unlock(&a_ctrl->send_window->lock);
        log_it(L_ERROR, "Send window full (capacity=%zu)", a_ctrl->send_window->capacity);
        return -1;
    }
    
    // Find free slot (circular buffer)
    size_t l_idx = a_seq_num % a_ctrl->send_window->capacity;
    
    // Should be free (we checked count above)
    if (a_ctrl->send_window->packets[l_idx].packet) {
        log_it(L_WARNING, "Send window slot collision at index %zu", l_idx);
        // Free old packet (shouldn't happen)
        a_ctrl->callbacks.packet_free(a_ctrl->send_window->packets[l_idx].packet,
                                     a_ctrl->callbacks.arg);
    }
    
    // Store packet
    a_ctrl->send_window->packets[l_idx].seq_num = a_seq_num;
    a_ctrl->send_window->packets[l_idx].packet = a_packet;
    a_ctrl->send_window->packets[l_idx].packet_size = a_packet_size;
    a_ctrl->send_window->packets[l_idx].send_time_ms = s_get_time_ms();
    a_ctrl->send_window->packets[l_idx].retransmit_count = 0;
    a_ctrl->send_window->packets[l_idx].acked = false;
    
    a_ctrl->send_window->count++;
    
    pthread_mutex_unlock(&a_ctrl->send_window->lock);
    
    return 0;
}

static void s_send_window_ack(dap_io_flow_ctrl_t *a_ctrl, uint64_t a_ack_seq)
{
    pthread_mutex_lock(&a_ctrl->send_window->lock);
    
    // ACK all packets up to and including a_ack_seq (cumulative ACK)
    for (uint64_t seq = a_ctrl->send_window->oldest_unacked_seq; seq <= a_ack_seq; seq++) {
        size_t l_idx = seq % a_ctrl->send_window->capacity;
        
        if (a_ctrl->send_window->packets[l_idx].packet &&
            a_ctrl->send_window->packets[l_idx].seq_num == seq &&
            !a_ctrl->send_window->packets[l_idx].acked) {
            
            // Free packet
            a_ctrl->callbacks.packet_free(a_ctrl->send_window->packets[l_idx].packet,
                                         a_ctrl->callbacks.arg);
            a_ctrl->send_window->packets[l_idx].packet = NULL;
            a_ctrl->send_window->packets[l_idx].acked = true;
            
            a_ctrl->send_window->count--;
            
            log_it(L_DEBUG, "ACKed packet seq=%lu", seq);
        }
    }
    
    // Move oldest_unacked_seq forward
    if (a_ack_seq >= a_ctrl->send_window->oldest_unacked_seq) {
        a_ctrl->send_window->oldest_unacked_seq = a_ack_seq + 1;
    }
    
    pthread_mutex_unlock(&a_ctrl->send_window->lock);
}

/**
 * @brief Retransmit timer callback (periodic check)
 */
static bool s_retransmit_timer_callback(void *a_arg)
{
    dap_io_flow_ctrl_t *l_ctrl = (dap_io_flow_ctrl_t*)a_arg;
    if (!l_ctrl || !l_ctrl->send_window) {
        return false;  // Stop timer
    }
    
    uint64_t l_now = s_get_time_ms();
    uint32_t l_timeout_ms = l_ctrl->config.retransmit_timeout_ms;
    
    pthread_mutex_lock(&l_ctrl->send_window->lock);
    
    // Check all tracked packets for timeout
    for (size_t i = 0; i < l_ctrl->send_window->capacity; i++) {
        dap_io_flow_ctrl_tracked_pkt_t *l_pkt = &l_ctrl->send_window->packets[i];
        
        if (!l_pkt->packet || l_pkt->acked) {
            continue;
        }
        
        uint64_t l_elapsed = l_now - l_pkt->send_time_ms;
        
        if (l_elapsed >= l_timeout_ms) {
            // Timeout! Retransmit or give up
            
            if (l_pkt->retransmit_count >= l_ctrl->config.max_retransmit_count) {
                // Max retries exceeded, consider lost
                log_it(L_WARNING, "Packet seq=%lu lost (max retries %u exceeded)",
                       l_pkt->seq_num, l_ctrl->config.max_retransmit_count);
                
                // Free packet
                l_ctrl->callbacks.packet_free(l_pkt->packet, l_ctrl->callbacks.arg);
                l_pkt->packet = NULL;
                
                l_ctrl->send_window->count--;
                
                pthread_mutex_lock(&l_ctrl->stats_lock);
                l_ctrl->stats.lost_packets++;
                pthread_mutex_unlock(&l_ctrl->stats_lock);
                
                continue;
            }
            
            // Retransmit
            log_it(L_DEBUG, "Retransmitting packet seq=%lu (attempt %u)",
                   l_pkt->seq_num, l_pkt->retransmit_count + 1);
            
            int l_ret = l_ctrl->callbacks.packet_send(
                l_ctrl->flow,
                l_pkt->packet,
                l_pkt->packet_size,
                l_ctrl->callbacks.arg);
            
            if (l_ret == 0) {
                l_pkt->send_time_ms = l_now;
                l_pkt->retransmit_count++;
                
                pthread_mutex_lock(&l_ctrl->stats_lock);
                l_ctrl->stats.retransmitted_packets++;
                pthread_mutex_unlock(&l_ctrl->stats_lock);
            } else {
                log_it(L_ERROR, "Retransmit failed for seq=%lu", l_pkt->seq_num);
            }
        }
    }
    
    pthread_mutex_unlock(&l_ctrl->send_window->lock);
    
    return true;  // Continue timer
}

// ============================================================================
// RECV WINDOW IMPLEMENTATION (Reordering)
// ============================================================================

static int s_recv_window_create(dap_io_flow_ctrl_t *a_ctrl)
{
    a_ctrl->recv_window = DAP_NEW_Z(dap_io_flow_ctrl_recv_window_t);
    if (!a_ctrl->recv_window) {
        return -1;
    }
    
    a_ctrl->recv_window->capacity = a_ctrl->config.recv_window_size;
    a_ctrl->recv_window->packets = DAP_NEW_Z_COUNT(dap_io_flow_ctrl_buffered_pkt_t,
                                                     a_ctrl->recv_window->capacity);
    if (!a_ctrl->recv_window->packets) {
        DAP_DELETE(a_ctrl->recv_window);
        a_ctrl->recv_window = NULL;
        return -2;
    }
    
    pthread_mutex_init(&a_ctrl->recv_window->lock, NULL);
    
    a_ctrl->recv_window->next_expected_seq = 1;  // Start from 1
    
    log_it(L_DEBUG, "Created recv window: capacity=%zu", a_ctrl->recv_window->capacity);
    
    return 0;
}

static void s_recv_window_destroy(dap_io_flow_ctrl_t *a_ctrl)
{
    if (!a_ctrl->recv_window) {
        return;
    }
    
    // Free all buffered payloads
    for (size_t i = 0; i < a_ctrl->recv_window->capacity; i++) {
        if (a_ctrl->recv_window->packets[i].valid) {
            DAP_DELETE(a_ctrl->recv_window->packets[i].payload);
        }
    }
    
    pthread_mutex_destroy(&a_ctrl->recv_window->lock);
    DAP_DEL_Z(a_ctrl->recv_window->packets);
    DAP_DELETE(a_ctrl->recv_window);
    a_ctrl->recv_window = NULL;
}

static int s_recv_window_buffer(dap_io_flow_ctrl_t *a_ctrl, uint64_t a_seq_num,
                                 const void *a_payload, size_t a_payload_size)
{
    pthread_mutex_lock(&a_ctrl->recv_window->lock);
    
    // Check if buffer is full
    if (a_ctrl->recv_window->count >= a_ctrl->recv_window->capacity) {
        pthread_mutex_unlock(&a_ctrl->recv_window->lock);
        log_it(L_WARNING, "Recv window full, dropping packet seq=%lu", a_seq_num);
        return -1;
    }
    
    // Find slot (circular buffer)
    size_t l_idx = a_seq_num % a_ctrl->recv_window->capacity;
    
    // Check if already buffered (duplicate out-of-order)
    if (a_ctrl->recv_window->packets[l_idx].valid &&
        a_ctrl->recv_window->packets[l_idx].seq_num == a_seq_num) {
        pthread_mutex_unlock(&a_ctrl->recv_window->lock);
        log_it(L_DEBUG, "Packet seq=%lu already buffered (duplicate)", a_seq_num);
        return 0;
    }
    
    // Free old payload if slot occupied (shouldn't happen)
    if (a_ctrl->recv_window->packets[l_idx].valid) {
        log_it(L_WARNING, "Recv window slot collision at index %zu", l_idx);
        DAP_DELETE(a_ctrl->recv_window->packets[l_idx].payload);
    }
    
    // Copy payload
    void *l_payload_copy = DAP_NEW_SIZE(uint8_t, a_payload_size);
    if (!l_payload_copy) {
        pthread_mutex_unlock(&a_ctrl->recv_window->lock);
        log_it(L_ERROR, "Failed to allocate payload copy");
        return -2;
    }
    
    memcpy(l_payload_copy, a_payload, a_payload_size);
    
    // Store
    a_ctrl->recv_window->packets[l_idx].seq_num = a_seq_num;
    a_ctrl->recv_window->packets[l_idx].payload = l_payload_copy;
    a_ctrl->recv_window->packets[l_idx].payload_size = a_payload_size;
    a_ctrl->recv_window->packets[l_idx].recv_time_ms = s_get_time_ms();
    a_ctrl->recv_window->packets[l_idx].valid = true;
    
    a_ctrl->recv_window->count++;
    
    if (a_seq_num > a_ctrl->recv_window->highest_received_seq) {
        a_ctrl->recv_window->highest_received_seq = a_seq_num;
    }
    
    pthread_mutex_unlock(&a_ctrl->recv_window->lock);
    
    log_it(L_DEBUG, "Buffered out-of-order packet seq=%lu", a_seq_num);
    
    return 0;
}

static int s_recv_window_deliver_inorder(dap_io_flow_ctrl_t *a_ctrl)
{
    pthread_mutex_lock(&a_ctrl->recv_window->lock);
    
    // Deliver all consecutive in-order packets from buffer
    while (true) {
        uint64_t l_next = a_ctrl->recv_window->next_expected_seq;
        size_t l_idx = l_next % a_ctrl->recv_window->capacity;
        
        dap_io_flow_ctrl_buffered_pkt_t *l_pkt = &a_ctrl->recv_window->packets[l_idx];
        
        if (!l_pkt->valid || l_pkt->seq_num != l_next) {
            // No more consecutive packets
            break;
        }
        
        // Deliver this packet
        pthread_mutex_unlock(&a_ctrl->recv_window->lock);
        
        int l_ret = a_ctrl->callbacks.payload_deliver(
            a_ctrl->flow,
            l_pkt->payload,
            l_pkt->payload_size,
            a_ctrl->callbacks.arg);
        
        if (l_ret != 0) {
            log_it(L_ERROR, "payload_deliver failed for buffered seq=%lu", l_next);
        }
        
        pthread_mutex_lock(&a_ctrl->recv_window->lock);
        
        // Free payload
        DAP_DELETE(l_pkt->payload);
        l_pkt->payload = NULL;
        l_pkt->valid = false;
        
        a_ctrl->recv_window->count--;
        a_ctrl->recv_window->next_expected_seq++;
        
        log_it(L_DEBUG, "Delivered buffered packet seq=%lu", l_next);
    }
    
    pthread_mutex_unlock(&a_ctrl->recv_window->lock);
    
    return 0;
}

// ============================================================================
// KEEP-ALIVE IMPLEMENTATION
// ============================================================================

static int s_keepalive_create(dap_io_flow_ctrl_t *a_ctrl)
{
    a_ctrl->keepalive = DAP_NEW_Z(dap_io_flow_ctrl_keepalive_t);
    if (!a_ctrl->keepalive) {
        return -1;
    }
    
    a_ctrl->keepalive->interval_ms = a_ctrl->config.keepalive_interval_ms;
    a_ctrl->keepalive->timeout_ms = a_ctrl->config.keepalive_timeout_ms;
    a_ctrl->keepalive->last_activity_ms = s_get_time_ms();
    
    // Create timer
    dap_worker_t *l_worker = dap_worker_get_current();
    if (l_worker) {
        a_ctrl->keepalive->timer = dap_timerfd_start_on_worker(
            l_worker,
            a_ctrl->keepalive->interval_ms,
            s_keepalive_timer_callback,
            a_ctrl);
        
        if (!a_ctrl->keepalive->timer) {
            DAP_DELETE(a_ctrl->keepalive);
            a_ctrl->keepalive = NULL;
            return -2;
        }
    }
    
    log_it(L_DEBUG, "Created keep-alive: interval=%ums, timeout=%ums",
           a_ctrl->keepalive->interval_ms, a_ctrl->keepalive->timeout_ms);
    
    return 0;
}

static void s_keepalive_destroy(dap_io_flow_ctrl_t *a_ctrl)
{
    if (!a_ctrl->keepalive) {
        return;
    }
    
    if (a_ctrl->keepalive->timer) {
        dap_timerfd_delete_mt(a_ctrl->keepalive->timer->events_socket->worker,
                             a_ctrl->keepalive->timer->events_socket->uuid);
    }
    
    DAP_DELETE(a_ctrl->keepalive);
    a_ctrl->keepalive = NULL;
}

/**
 * @brief Keep-alive timer callback
 */
static bool s_keepalive_timer_callback(void *a_arg)
{
    dap_io_flow_ctrl_t *l_ctrl = (dap_io_flow_ctrl_t*)a_arg;
    if (!l_ctrl || !l_ctrl->keepalive) {
        return false;  // Stop timer
    }
    
    uint64_t l_now = s_get_time_ms();
    uint64_t l_elapsed = l_now - l_ctrl->keepalive->last_activity_ms;
    
    // Check if connection timed out
    if (l_elapsed >= l_ctrl->keepalive->timeout_ms) {
        log_it(L_WARNING, "Keep-alive timeout (elapsed=%lums, timeout=%ums)",
               l_elapsed, l_ctrl->keepalive->timeout_ms);
        
        // Call timeout callback if provided
        if (l_ctrl->callbacks.keepalive_timeout) {
            l_ctrl->callbacks.keepalive_timeout(l_ctrl->flow, l_ctrl->callbacks.arg);
        }
        
        return false;  // Stop timer (connection dead)
    }
    
    // Send keep-alive packet
    log_it(L_DEBUG, "Sending keep-alive (last activity %lums ago)", l_elapsed);
    
    // Prepare metadata for keep-alive
    dap_io_flow_pkt_metadata_t l_metadata = {0};
    l_metadata.is_keepalive = true;
    l_metadata.timestamp_ms = l_now;
    
    if (l_ctrl->recv_window) {
        pthread_mutex_lock(&l_ctrl->recv_window->lock);
        l_metadata.ack_seq = l_ctrl->recv_window->next_expected_seq - 1;
        pthread_mutex_unlock(&l_ctrl->recv_window->lock);
    }
    
    // Prepare packet (empty payload for keep-alive)
    void *l_packet = NULL;
    size_t l_packet_size = 0;
    
    int l_ret = l_ctrl->callbacks.packet_prepare(
        l_ctrl->flow,
        &l_metadata,
        NULL,  // No payload
        0,
        &l_packet,
        &l_packet_size,
        l_ctrl->callbacks.arg);
    
    if (l_ret == 0 && l_packet) {
        // Send keep-alive
        l_ctrl->callbacks.packet_send(l_ctrl->flow, l_packet, l_packet_size,
                                     l_ctrl->callbacks.arg);
        l_ctrl->callbacks.packet_free(l_packet, l_ctrl->callbacks.arg);
    }
    
    return true;  // Continue timer
}

