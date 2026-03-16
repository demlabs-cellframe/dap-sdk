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
#include <errno.h>
#include <time.h>
#include "dap_common.h"
#include "dap_time.h"
#include "dap_io_flow_ctrl.h"
#include "dap_io_flow.h"
#include "dap_timerfd.h"
#include "dap_worker.h"
#include "dap_arena.h"
#include "dap_config.h"

#define LOG_TAG "dap_io_flow_ctrl"

static bool s_debug_more = false;

// Magic number for FC structure validation (detect use-after-free)
#define DAP_IO_FLOW_CTRL_MAGIC 0xFC10C001

//===================================================================
// BASE FLOW CONTROL HEADER SCHEMA
//===================================================================

/**
 * @brief Base Flow Control header serialization schema
 * 
 * Protocols can extend this schema with additional fields using
 * DAP_SERIALIZE_SCHEMA_EXTEND() macro.
 */
const dap_serialize_field_t g_dap_io_flow_ctrl_base_fields[] = {
    {
        .name = "seq_num",
        .type = DAP_SERIALIZE_TYPE_UINT64,
        .flags = DAP_SERIALIZE_FLAG_BIG_ENDIAN,
        .offset = offsetof(dap_io_flow_ctrl_base_header_t, seq_num),
        .size = sizeof(uint64_t),
    },
    {
        .name = "ack_seq",
        .type = DAP_SERIALIZE_TYPE_UINT64,
        .flags = DAP_SERIALIZE_FLAG_BIG_ENDIAN,
        .offset = offsetof(dap_io_flow_ctrl_base_header_t, ack_seq),
        .size = sizeof(uint64_t),
    },
    {
        .name = "timestamp_ms",
        .type = DAP_SERIALIZE_TYPE_UINT32,
        .flags = DAP_SERIALIZE_FLAG_BIG_ENDIAN,
        .offset = offsetof(dap_io_flow_ctrl_base_header_t, timestamp_ms),
        .size = sizeof(uint32_t),
    },
    {
        .name = "flags",
        .type = DAP_SERIALIZE_TYPE_UINT8,
        .flags = DAP_SERIALIZE_FLAG_NONE,
        .offset = offsetof(dap_io_flow_ctrl_base_header_t, flags),
        .size = sizeof(uint8_t),
    },
};

const size_t g_dap_io_flow_ctrl_base_field_count = 
    sizeof(g_dap_io_flow_ctrl_base_fields) / sizeof(g_dap_io_flow_ctrl_base_fields[0]);

//===================================================================
// INTERNAL STRUCTURES
//===================================================================

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
    void *packet_buffer;            // Original packet buffer from packet_parse (to free after delivery)
} recv_window_entry_t;

struct dap_io_flow_ctrl {
    uint32_t magic;                     // Magic number for validation (DAP_IO_FLOW_CTRL_MAGIC)
    dap_io_flow_t *flow;            // Associated flow
    dap_io_flow_ctrl_flags_t flags; // Active flags
    dap_io_flow_ctrl_config_t config; // Configuration
    dap_io_flow_ctrl_callbacks_t callbacks; // Transport callbacks
    
    // Lifecycle management (prevents use-after-free in multithreaded scenarios)
    _Atomic(int32_t) active_ops;        // Count of active operations (send/recv)
    _Atomic(bool) deleting;             // Flag: deletion in progress
    pthread_mutex_t lifecycle_mutex;    // Mutex for lifecycle synchronization
    pthread_cond_t lifecycle_cond;      // Condition: wait for operations to complete
    
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
// LIFECYCLE MANAGEMENT (prevents use-after-free in multithreaded code)
//===================================================================

/**
 * @brief Begin an operation on flow control
 * 
 * Must be called at the start of any public API function that accesses FC data.
 * Returns true if operation can proceed, false if FC is being deleted.
 * If returns true, MUST call dap_io_flow_ctrl_op_end() when done.
 * 
 * Thread-safe: uses atomic operations with proper ordering.
 */
static inline bool s_op_begin(dap_io_flow_ctrl_t *a_ctrl)
{
    // Fast path: check if already deleting
    if (atomic_load_explicit(&a_ctrl->deleting, memory_order_acquire)) {
        return false;
    }
    
    // Increment active operations counter
    atomic_fetch_add_explicit(&a_ctrl->active_ops, 1, memory_order_acq_rel);
    
    // Double-check after increment (handles race with delete starting)
    if (atomic_load_explicit(&a_ctrl->deleting, memory_order_acquire)) {
        // Delete started while we were incrementing - abort
        int32_t l_ops = atomic_fetch_sub_explicit(&a_ctrl->active_ops, 1, memory_order_acq_rel);
        if (l_ops == 1) {
            // We were the last one, signal delete can proceed
            pthread_mutex_lock(&a_ctrl->lifecycle_mutex);
            pthread_cond_signal(&a_ctrl->lifecycle_cond);
            pthread_mutex_unlock(&a_ctrl->lifecycle_mutex);
        }
        return false;
    }
    
    return true;
}

/**
 * @brief End an operation on flow control
 * 
 * Must be called after s_op_begin() returned true.
 * Signals deletion thread if this was the last active operation.
 */
static inline void s_op_end(dap_io_flow_ctrl_t *a_ctrl)
{
    int32_t l_ops = atomic_fetch_sub_explicit(&a_ctrl->active_ops, 1, memory_order_acq_rel);
    
    // If we were the last operation AND deletion is in progress, signal
    if (l_ops == 1 && atomic_load_explicit(&a_ctrl->deleting, memory_order_acquire)) {
        pthread_mutex_lock(&a_ctrl->lifecycle_mutex);
        pthread_cond_signal(&a_ctrl->lifecycle_cond);
        pthread_mutex_unlock(&a_ctrl->lifecycle_mutex);
    }
}

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
    
    // Read debug settings from config
    s_debug_more = dap_config_get_item_bool_default(g_config, "io_flow", "debug_more", false);
    
    log_it(L_NOTICE, "Flow Control subsystem initialized (debug_more=%s)", 
           s_debug_more ? "true" : "false");
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
    
    // Set magic number for validation (detect use-after-free in timer callbacks)
    l_ctrl->magic = DAP_IO_FLOW_CTRL_MAGIC;
    
    // Initialize lifecycle management
    atomic_init(&l_ctrl->active_ops, 0);
    atomic_init(&l_ctrl->deleting, false);
    pthread_mutex_init(&l_ctrl->lifecycle_mutex, NULL);
    pthread_cond_init(&l_ctrl->lifecycle_cond, NULL);
    
    debug_if(s_debug_more, L_DEBUG, "FC CREATE: l_ctrl=%p, a_flow=%p, payload_deliver=%p, arg=%p",
             l_ctrl, a_flow, a_callbacks->payload_deliver, a_callbacks->arg);
    
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
            } else {
                log_it(L_INFO, "FC: Retransmit timer started on worker %u (interval=%ums)",
                       l_worker->id, l_ctrl->config.retransmit_timeout_ms / 2);
            }
        } else {
            log_it(L_ERROR, "FC CREATE WARNING: No worker context! Retransmit timer NOT created. "
                   "Packets will NOT be retransmitted on loss!");
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
 * 
 * This function is SYNCHRONOUS - it waits for all active operations (send/recv)
 * to complete before freeing resources. This prevents use-after-free in
 * multithreaded scenarios where multiple workers may be using the same flow control.
 */
void dap_io_flow_ctrl_delete(dap_io_flow_ctrl_t *a_ctrl)
{
    if (!a_ctrl) {
        return;
    }
    
    // STEP 1: Signal that deletion is in progress
    // This will cause new operations to fail fast via s_op_begin()
    atomic_store_explicit(&a_ctrl->deleting, true, memory_order_release);
    
    // STEP 2: Wait for all active operations to complete
    // Any thread currently inside send/recv will finish and call s_op_end()
    pthread_mutex_lock(&a_ctrl->lifecycle_mutex);
    while (atomic_load_explicit(&a_ctrl->active_ops, memory_order_acquire) > 0) {
        // Use timed wait to detect stuck operations (debug aid)
        struct timespec l_timeout;
        clock_gettime(CLOCK_REALTIME, &l_timeout);
        l_timeout.tv_sec += 5;  // 5 second timeout for debug
        
        int l_ret = pthread_cond_timedwait(&a_ctrl->lifecycle_cond, 
                                            &a_ctrl->lifecycle_mutex, 
                                            &l_timeout);
        if (l_ret == ETIMEDOUT) {
            int32_t l_ops = atomic_load_explicit(&a_ctrl->active_ops, memory_order_acquire);
            log_it(L_WARNING, "FC delete: waiting for %d active operations to complete...", l_ops);
        }
    }
    pthread_mutex_unlock(&a_ctrl->lifecycle_mutex);
    
    // STEP 3: Clear magic under send_mutex to synchronize with timer callbacks
    // Timer callbacks check magic before accessing FC data
    if (a_ctrl->send_window) {
        pthread_mutex_lock(&a_ctrl->send_mutex);
        a_ctrl->magic = 0;
        pthread_mutex_unlock(&a_ctrl->send_mutex);
    } else {
        a_ctrl->magic = 0;
    }
    
    // STEP 4: Stop timers (any running callbacks will exit due to magic=0)
    if (a_ctrl->retransmit_timer) {
        dap_timerfd_delete_unsafe(a_ctrl->retransmit_timer);
        a_ctrl->retransmit_timer = NULL;
    }
    if (a_ctrl->keepalive_timer) {
        dap_timerfd_delete_unsafe(a_ctrl->keepalive_timer);
        a_ctrl->keepalive_timer = NULL;
    }
    
    // STEP 5: Clean send window
    if (a_ctrl->send_window) {
        for (size_t i = 0; i < a_ctrl->send_window_size; i++) {
            if (a_ctrl->send_window[i].packet) {
                a_ctrl->callbacks.packet_free(a_ctrl->send_window[i].packet, a_ctrl->callbacks.arg);
            }
        }
        pthread_mutex_destroy(&a_ctrl->send_mutex);
        DAP_DEL_Z(a_ctrl->send_window);
    }
    
    // STEP 6: Clean receive window
    if (a_ctrl->recv_window) {
        for (size_t i = 0; i < a_ctrl->recv_window_size; i++) {
            DAP_DEL_Z(a_ctrl->recv_window[i].payload);
            if (a_ctrl->recv_window[i].packet_buffer) {
                DAP_DELETE(a_ctrl->recv_window[i].packet_buffer);
            }
        }
        pthread_mutex_destroy(&a_ctrl->recv_mutex);
        DAP_DEL_Z(a_ctrl->recv_window);
    }
    
    // STEP 7: Destroy lifecycle synchronization primitives
    pthread_cond_destroy(&a_ctrl->lifecycle_cond);
    pthread_mutex_destroy(&a_ctrl->lifecycle_mutex);
    
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
    
    // Default retransmit configuration
    // Tuned for UDP with potential network delays and VERY large transfers (gigabytes)
    // Window size = 65536 packets allows ~64MB in-flight at 1KB/packet
    a_config->retransmit_timeout_ms = 1000;  // 1 second
    a_config->max_retransmit_count = 5;
    a_config->send_window_size = 65536;  // 64K packets (~64MB for 1KB packets)
    a_config->recv_window_size = 65536;  // 64K packets reorder buffer  
    a_config->max_out_of_order_delay_ms = 5000;  // 5 seconds for large transfers
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
    
    dap_io_flow_ctrl_flags_t l_old_flags = a_ctrl->flags;
    a_ctrl->flags = a_flags;
    
    // Handle timer changes based on flag transitions
    dap_worker_t *l_worker = dap_worker_get_current();
    
    // Retransmit timer
    if ((a_flags & DAP_IO_FLOW_CTRL_RETRANSMIT) && !(l_old_flags & DAP_IO_FLOW_CTRL_RETRANSMIT)) {
        // Enable retransmit - start timer
        if (l_worker && !a_ctrl->retransmit_timer) {
            a_ctrl->retransmit_timer = dap_timerfd_start_on_worker(
                l_worker,
                a_ctrl->config.retransmit_timeout_ms / 2,
                s_retransmit_timer_callback,
                a_ctrl
            );
        }
    } else if (!(a_flags & DAP_IO_FLOW_CTRL_RETRANSMIT) && (l_old_flags & DAP_IO_FLOW_CTRL_RETRANSMIT)) {
        // Disable retransmit - stop timer
        if (a_ctrl->retransmit_timer) {
            dap_timerfd_delete_unsafe(a_ctrl->retransmit_timer);
            a_ctrl->retransmit_timer = NULL;
        }
    }
    
    // Keepalive timer
    if ((a_flags & DAP_IO_FLOW_CTRL_KEEPALIVE) && !(l_old_flags & DAP_IO_FLOW_CTRL_KEEPALIVE)) {
        // Enable keepalive - start timer
        if (l_worker && !a_ctrl->keepalive_timer) {
            a_ctrl->keepalive_timer = dap_timerfd_start_on_worker(
                l_worker,
                a_ctrl->config.keepalive_interval_ms / 2,
                s_keepalive_timer_callback,
                a_ctrl
            );
        }
    } else if (!(a_flags & DAP_IO_FLOW_CTRL_KEEPALIVE) && (l_old_flags & DAP_IO_FLOW_CTRL_KEEPALIVE)) {
        // Disable keepalive - stop timer
        if (a_ctrl->keepalive_timer) {
            dap_timerfd_delete_unsafe(a_ctrl->keepalive_timer);
            a_ctrl->keepalive_timer = NULL;
        }
    }
    
    log_it(L_DEBUG, "Flow Control flags updated: 0x%02x → 0x%02x", l_old_flags, a_flags);
    
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
    
    // LIFECYCLE: Begin operation (prevents deletion during send)
    if (!s_op_begin(a_ctrl)) {
        // Flow control is being deleted - fail fast
        debug_if(s_debug_more, L_DEBUG, "FC send: rejected - FC is being deleted");
        return -10;  // Distinct error code for "deleted"
    }
    
    // Assign sequence number
    uint64_t l_seq_num = 0;
    if (a_ctrl->flags & DAP_IO_FLOW_CTRL_RETRANSMIT) {
        pthread_mutex_lock(&a_ctrl->send_mutex);
        l_seq_num = a_ctrl->send_seq_next++;
        pthread_mutex_unlock(&a_ctrl->send_mutex);
        
        
        debug_if(s_debug_more, L_DEBUG,
                 "FC send: assigned seq=%lu (flags=0x%02x, send_seq_next=%lu)",
                 l_seq_num, a_ctrl->flags, a_ctrl->send_seq_next);
    } else {
        
        debug_if(s_debug_more, L_DEBUG,
                 "FC send: NO RETRANSMIT flag, seq=0 (flags=0x%02x)",
                 a_ctrl->flags);
    }
    
    // Prepare metadata
    dap_io_flow_pkt_metadata_t l_metadata = {
        .seq_num = l_seq_num,
        .ack_seq = (a_ctrl->flags & DAP_IO_FLOW_CTRL_REORDER) ? a_ctrl->recv_seq_expected - 1 : 0,
        .timestamp_ms = (uint32_t)(dap_nanotime_now() / 1000000),
        .is_keepalive = false,
        .is_retransmit = false,
    };
    
    debug_if(s_debug_more, L_DEBUG,
             "FC send: metadata prepared: seq=%lu, ack=%lu, ts=%u, &l_metadata=%p",
             l_metadata.seq_num, l_metadata.ack_seq, l_metadata.timestamp_ms, &l_metadata);
    
    // Prepare packet (add header)
    void *l_packet = NULL;
    size_t l_packet_size = 0;
    
    debug_if(s_debug_more, L_DEBUG,
             "FC send: calling packet_prepare with &l_metadata=%p", &l_metadata);
    
    int l_ret = a_ctrl->callbacks.packet_prepare(a_ctrl->flow, &l_metadata, a_payload, a_payload_size,
                                                  &l_packet, &l_packet_size, a_ctrl->callbacks.arg);
    if (l_ret != 0 || !l_packet) {
        log_it(L_ERROR, "Failed to prepare packet: ret=%d", l_ret);
        s_op_end(a_ctrl);  // LIFECYCLE: End operation
        return -2;
    }
    
    // Send packet
    l_ret = a_ctrl->callbacks.packet_send(a_ctrl->flow, l_packet, l_packet_size, a_ctrl->callbacks.arg);
    if (l_ret != 0) {
        log_it(L_WARNING, "Failed to send packet: ret=%d", l_ret);
        a_ctrl->callbacks.packet_free(l_packet, a_ctrl->callbacks.arg);
        s_op_end(a_ctrl);  // LIFECYCLE: End operation
        return -3;
    }
    
    // Track for retransmission if enabled
    if (a_ctrl->flags & DAP_IO_FLOW_CTRL_RETRANSMIT) {
        // CRITICAL: Validate send_window exists
        // This can fail if FC was created without proper initialization
        // or if there's a race condition with deletion
        if (!a_ctrl->send_window || a_ctrl->send_window_size == 0) {
            log_it(L_ERROR, "FC send: CRITICAL - send_window=%p, send_window_size=%zu (flags=0x%02x). "
                   "FC not properly initialized or already deleted!",
                   a_ctrl->send_window, a_ctrl->send_window_size, a_ctrl->flags);
            // Still free the packet to avoid memory leak
            a_ctrl->callbacks.packet_free(l_packet, a_ctrl->callbacks.arg);
            s_op_end(a_ctrl);  // LIFECYCLE: End operation
            return -4;
        }
        
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
    
    s_op_end(a_ctrl);  // LIFECYCLE: End operation
    return 0;
}

/**
 * @brief Process received packet
 */
int dap_io_flow_ctrl_recv(dap_io_flow_ctrl_t *a_ctrl, const void *a_packet, size_t a_packet_size)
{
    debug_if(s_debug_more, L_DEBUG, "dap_io_flow_ctrl_recv ENTRY: a_ctrl=%p", a_ctrl);
    
    if (!a_ctrl || !a_packet || a_packet_size == 0) {
        return -1;
    }
    
    // LIFECYCLE: Begin operation (prevents deletion during recv)
    if (!s_op_begin(a_ctrl)) {
        // Flow control is being deleted - fail fast
        debug_if(s_debug_more, L_DEBUG, "FC recv: rejected - FC is being deleted");
        return -10;  // Distinct error code for "deleted"
    }
    
    // Parse packet
    dap_io_flow_pkt_metadata_t l_metadata = {0};
    const void *l_payload = NULL;
    size_t l_payload_size = 0;
    
    int l_ret = a_ctrl->callbacks.packet_parse(a_ctrl->flow, a_packet, a_packet_size,
                                                &l_metadata, &l_payload, &l_payload_size,
                                                a_ctrl->callbacks.arg);
    
    debug_if(s_debug_more, L_DEBUG, "packet_parse returned: ret=%d, seq=%lu, payload_size=%zu", 
             l_ret, l_metadata.seq_num, l_payload_size);
    
    if (l_ret != 0) {
        debug_if(s_debug_more, L_WARNING, "Failed to parse packet: ret=%d", l_ret);
        s_op_end(a_ctrl);  // LIFECYCLE: End operation
        return -2;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "FC recv: seq=%lu, ack_seq=%lu, is_keepalive=%d, payload_size=%zu, reorder_enabled=%d",
             l_metadata.seq_num, l_metadata.ack_seq, l_metadata.is_keepalive, l_payload_size,
             (a_ctrl->flags & DAP_IO_FLOW_CTRL_REORDER) ? 1 : 0);
    
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
            debug_if(s_debug_more, L_DEBUG, "FC recv: UPDATING send_seq_acked: %lu → %lu",
                   a_ctrl->send_seq_acked, l_metadata.ack_seq);
            a_ctrl->send_seq_acked = l_metadata.ack_seq;
        }
        
        pthread_mutex_unlock(&a_ctrl->send_mutex);
    }
    
    // Handle keep-alive packet
    if (l_metadata.is_keepalive) {
        debug_if(true, L_DEBUG, "Received keep-alive packet");
        s_op_end(a_ctrl);  // LIFECYCLE: End operation
        return 0;
    }
    
    // Deliver payload
    if (l_payload_size > 0) {
        if (a_ctrl->flags & DAP_IO_FLOW_CTRL_REORDER) {
            // REORDERING ENABLED: Buffer out-of-order packets
            pthread_mutex_lock(&a_ctrl->recv_mutex);
            
            uint64_t l_seq = l_metadata.seq_num;
            
            debug_if(s_debug_more, L_DEBUG, "FC recv: REORDER CHECK: seq=%lu, recv_seq_expected=%lu", 
                   l_seq, a_ctrl->recv_seq_expected);
            
            // Check if this is the expected sequence
            if (l_seq == a_ctrl->recv_seq_expected) {
                // IN-ORDER: Deliver immediately + check buffered packets
                
                // Deliver payload
                debug_if(s_debug_more, L_DEBUG, "FC recv: CALLING payload_deliver (flow=%p, payload_size=%zu, arg=%p)",
                       a_ctrl->flow, l_payload_size, a_ctrl->callbacks.arg);
                
                int l_deliver_ret = a_ctrl->callbacks.payload_deliver(a_ctrl->flow, l_payload, l_payload_size, a_ctrl->callbacks.arg);
                
                debug_if(s_debug_more, L_DEBUG, "FC recv: payload_deliver RETURNED: ret=%d", l_deliver_ret);
                
                // Free packet buffer after immediate delivery
                // packet_parse stored buffer pointer in metadata->private_ctx
                if (l_metadata.private_ctx) {
                    DAP_DELETE(l_metadata.private_ctx);
                }
                
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
                        // Free original packet buffer
                        if (a_ctrl->recv_window[l_idx].packet_buffer) {
                            DAP_DELETE(a_ctrl->recv_window[l_idx].packet_buffer);
                            a_ctrl->recv_window[l_idx].packet_buffer = NULL;
                        }
                        a_ctrl->recv_window[l_idx].received = false;
                        a_ctrl->recv_seq_expected++;
                        atomic_fetch_add(&a_ctrl->stats_recv, 1);
                    } else {
                        break;  // No more consecutive packets
                    }
                }
            } else if (l_seq > a_ctrl->recv_seq_expected) {
                // OUT-OF-ORDER: Buffer for later delivery
                debug_if(s_debug_more, L_INFO, "FC recv: BUFFERING OUT-OF-ORDER seq=%lu (expected=%lu)", 
                       l_seq, a_ctrl->recv_seq_expected);
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
                    if (a_ctrl->recv_window[l_idx].packet_buffer) {
                        DAP_DELETE(a_ctrl->recv_window[l_idx].packet_buffer);
                    }
                    
                    a_ctrl->recv_window[l_idx].payload = DAP_NEW_SIZE(uint8_t, l_payload_size);
                    if (a_ctrl->recv_window[l_idx].payload) {
                        memcpy(a_ctrl->recv_window[l_idx].payload, l_payload, l_payload_size);
                        a_ctrl->recv_window[l_idx].payload_size = l_payload_size;
                        a_ctrl->recv_window[l_idx].seq_num = l_seq;
                        a_ctrl->recv_window[l_idx].received = true;
                        a_ctrl->recv_window[l_idx].packet_buffer = l_metadata.private_ctx;  // Save buffer for later free
                        
                        if (l_seq > a_ctrl->recv_seq_highest) {
                            a_ctrl->recv_seq_highest = l_seq;
                        }
                        
                        log_it(L_DEBUG, "Buffered out-of-order packet: seq=%lu, expected=%lu", 
                               l_seq, a_ctrl->recv_seq_expected);
                        atomic_fetch_add(&a_ctrl->stats_out_of_order, 1);
                    } else {
                        log_it(L_ERROR, "Failed to allocate buffer for out-of-order packet");
                        // Free packet buffer since we can't buffer
                        if (l_metadata.private_ctx) {
                            DAP_DELETE(l_metadata.private_ctx);
                        }
                    }
                }
            } else {
                // DUPLICATE or REPLAY: seq < expected
                debug_if(s_debug_more, L_NOTICE, "Old/duplicate packet: seq=%lu, expected=%lu", 
                       l_seq, a_ctrl->recv_seq_expected);
                atomic_fetch_add(&a_ctrl->stats_duplicate, 1);
            }
            
            pthread_mutex_unlock(&a_ctrl->recv_mutex);
        } else {
            // NO REORDERING: Deliver immediately
            a_ctrl->callbacks.payload_deliver(a_ctrl->flow, l_payload, l_payload_size, a_ctrl->callbacks.arg);
            
            // Free packet buffer after delivery
            if (l_metadata.private_ctx) {
                DAP_DELETE(l_metadata.private_ctx);
            }
            
            atomic_fetch_add(&a_ctrl->stats_recv, 1);
        }
    }
    
    // Send ACK ONLY if RETRANSMIT enabled AND we have valid data to acknowledge
    if ((a_ctrl->flags & DAP_IO_FLOW_CTRL_RETRANSMIT) && l_metadata.seq_num > 0 && !l_metadata.is_keepalive) {
        // Determine if we should send ACK and what ack_seq to use
        bool l_should_send_ack = false;
        uint64_t l_ack_seq_to_send = 0;
        
        if (a_ctrl->flags & DAP_IO_FLOW_CTRL_REORDER) {
            // REORDERING enabled: Send ACK ONLY if we delivered in-order packets
            // (recv_seq_expected increased beyond initial state)
            pthread_mutex_lock(&a_ctrl->recv_mutex);
            debug_if(s_debug_more, L_DEBUG, "FC recv: ACK decision: recv_seq_expected=%lu, should_send=%s",
                   a_ctrl->recv_seq_expected, (a_ctrl->recv_seq_expected > 1) ? "YES" : "NO");
            if (a_ctrl->recv_seq_expected > 1) {
                l_should_send_ack = true;
                l_ack_seq_to_send = a_ctrl->recv_seq_expected - 1;
            }
            pthread_mutex_unlock(&a_ctrl->recv_mutex);
            
            // Don't send ACK for OUT-OF-ORDER packets - wait until missing packets arrive
            if (!l_should_send_ack) {
                debug_if(s_debug_more, L_DEBUG,
                         "FC recv: NOT sending ACK for out-of-order seq=%lu (expected=1, will ACK after delivery)",
                         l_metadata.seq_num);
            }
        } else {
            // NO reordering: Always ACK the received packet
            l_should_send_ack = true;
            l_ack_seq_to_send = l_metadata.seq_num;
        }
        
        if (l_should_send_ack && l_ack_seq_to_send > 0) {
            // Prepare ACK-only packet (no payload)
            dap_io_flow_pkt_metadata_t l_ack_metadata = {
                .seq_num = 0,  // ACK-only packets don't need seq_num
                .ack_seq = l_ack_seq_to_send,
                .timestamp_ms = (uint32_t)(dap_nanotime_now() / 1000000),
                .is_keepalive = false,
                .is_retransmit = false,
            };
            
            debug_if(s_debug_more, L_DEBUG, "FC recv: Preparing ACK for seq_num up to %lu", l_ack_seq_to_send);
            
            void *l_ack_packet = NULL;
            size_t l_ack_packet_size = 0;
            int l_ack_ret = a_ctrl->callbacks.packet_prepare(a_ctrl->flow, &l_ack_metadata, NULL, 0,
                                                              &l_ack_packet, &l_ack_packet_size, a_ctrl->callbacks.arg);
            debug_if(s_debug_more, L_DEBUG, "FC recv: packet_prepare returned: ret=%d, packet=%p, size=%zu",
                   l_ack_ret, l_ack_packet, l_ack_packet_size);
            
            if (l_ack_ret == 0 && l_ack_packet) {
                l_ack_ret = a_ctrl->callbacks.packet_send(a_ctrl->flow, l_ack_packet, l_ack_packet_size,
                                                           a_ctrl->callbacks.arg);
                debug_if(s_debug_more, L_DEBUG, "FC recv: packet_send returned: ret=%d", l_ack_ret);
                
                a_ctrl->callbacks.packet_free(l_ack_packet, a_ctrl->callbacks.arg);
                
                if (l_ack_ret == 0) {
                    debug_if(s_debug_more, L_DEBUG, "Sent ACK: ack_seq=%lu", l_ack_seq_to_send);
                } else {
                    log_it(L_WARNING, "Failed to send ACK packet: ret=%d", l_ack_ret);
                }
            }
        }
    }
    
    s_op_end(a_ctrl);  // LIFECYCLE: End operation
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
    
    // CRITICAL: Check magic WITHOUT mutex first (fast path for deleted FC)
    if (!l_ctrl || l_ctrl->magic != DAP_IO_FLOW_CTRL_MAGIC) {
        return false;  // Stop timer - FC deleted
    }
    
    // Lock mutex BEFORE accessing any FC data to prevent race with dap_io_flow_ctrl_delete()
    pthread_mutex_lock(&l_ctrl->send_mutex);
    
    // Double-check magic AFTER acquiring lock (detect deletion during lock wait)
    if (l_ctrl->magic != DAP_IO_FLOW_CTRL_MAGIC) {
        pthread_mutex_unlock(&l_ctrl->send_mutex);
        return false;  // Stop timer - FC deleted while waiting for lock
    }
    
    uint64_t l_now = dap_nanotime_now();
    uint64_t l_timeout_ns = l_ctrl->config.retransmit_timeout_ms * 1000000ULL;
    
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
    
    // Check magic (no mutex needed for keepalive - it doesn't modify send window)
    if (!l_ctrl || l_ctrl->magic != DAP_IO_FLOW_CTRL_MAGIC) {
        return false;  // Stop timer - FC deleted
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

