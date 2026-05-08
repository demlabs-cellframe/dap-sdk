/**
 * @file dap_send_olb.h
 * @brief Advanced/internal — send-side OLB adapter (write on processor, flush on worker).
 *
 * Lower-level than @ref dap_io_send.h for typical sends; normal docs steer integrators
 * to @c dap_io_tx_send* first.
 *
 * Provides the network send path on top of the core OLB structure
 * defined in dap_vmqueue_olb.h.  Separated to keep the core
 * data structure free of socket/network dependencies.
 *
 * Send OLB data flow (single-producer / single-consumer):
 *
 *   Processor thread (writer)          Worker thread (reader)
 *   ─────────────────────────          ──────────────────────
 *        dap_send_olb_write()               dap_send_olb_flush()
 *              │                                   │
 *              ▼                                   ▼
 *   ┌──────────────────────────────────────────────────┐
 *   │  OLB buffer   [head_pos ............. tail_pos]  │
 *   │               ◄── readable ──►                   │
 *   └──────────────────────────────────────────────────┘
 *              │                                   │
 *     memcpy into buffer,                   send() to socket,
 *     advance write_end                     advance head_pos
 *     then publish tail_pos                 (frees space for writer)
 *
 *   Cursors (send_olb context):
 *     write_end  — local to writer, raw byte offset after last memcpy
 *     tail_pos   — (atomic) committed boundary, published after write
 *     head_pos   — (atomic) consumer's read cursor, advanced after send()
 *     compacted  — (atomic) epoch flag: writer signals wrap-around to reader
 *
 *   Wrap-around: when write_end reaches capacity, the writer resets
 *   write_end and tail_pos to 0 and sets compacted=1.  The reader
 *   detects compacted, resets head_pos to 0 and re-reads tail_pos.
 *
 *   Note: dap_io_conn_open() allocates recv and send OLBs with the
 *   same capacity (a_olb_cap), so forwarding a complete recv frame
 *   into the send OLB can never hit TOO_LARGE.
 */
#pragma once

#include "dap_vmqueue_olb.h"

#ifdef DAP_OS_WINDOWS
#  include <winsock2.h>
#else
#  include <sys/socket.h>
#  include <sched.h>
#endif

/**
 * Low-level OLB write result.  The public write surface is
 * dap_io_tx_send() / dap_io_tx_send_direct(); this enum is the
 * underlying primitive and is exposed only for the send-OLB
 * unit tests (test_stress) and explicit batch/test send paths.
 *
 *   OK        — data copied into the buffer
 *   FULL      — transient, buffer has no contiguous space right now
 *   TOO_LARGE — permanent, a_len > capacity; caller must chunk
 */
typedef enum {
    DAP_SEND_OLB_OK = 0,
    DAP_SEND_OLB_FULL,
    DAP_SEND_OLB_TOO_LARGE
} dap_send_olb_result_t;

/**
 * @brief Producer-view free space in the send OLB (bytes).
 *
 * Computes the distance between the published tail and the consumer's
 * head.  Safe to call from the producer thread (the one that calls
 * dap_send_olb_write()).  A post-wrap transient window may return
 * capacity even though the consumer's head has not yet reset; this is
 * benign — the subsequent flush on the consumer side reconciles it.
 *
 * Used by dap_io_tx_send() to decide when to raise the SUSPENDED flag
 * as a send-side watermark (symmetric to recv-side OLB_FULL).
 */
DAP_STATIC_INLINE size_t
dap_send_olb_free(dap_vmqueue_olb_t *a_q)
{
    uint64_t l_t = atomic_load_explicit(&a_q->tail_pos, memory_order_relaxed);
    uint64_t l_h = atomic_load_explicit(&a_q->head_pos, memory_order_acquire);
    size_t l_used = (l_t >= l_h) ? (size_t)(l_t - l_h) : 0;
    return a_q->capacity > l_used ? a_q->capacity - l_used : 0;
}

/**
 * @brief All-or-nothing write into send OLB (single-producer primitive).
 *
 * Fast path: space between write_end and capacity — memcpy and publish.
 * Slow path: wrap to 0 if the reader has caught up, otherwise FULL.
 *
 * Not a user-facing API — call dap_io_tx_send_direct() / dap_io_tx_send()
 * instead.  This primitive is used by explicit batch/test send paths and
 * the send-OLB unit test.
 *
 * @return OK on success, FULL if worker hasn't drained enough yet,
 *         TOO_LARGE if a_len exceeds the entire buffer capacity.
 */
DAP_STATIC_INLINE dap_send_olb_result_t
dap_send_olb_write(dap_vmqueue_olb_t *a_q, const void *a_data, size_t a_len)
{
    if (__builtin_expect(a_q->write_end + a_len <= a_q->capacity, 1))
        goto do_write;
    if (__builtin_expect(a_len > a_q->capacity, 0))
        return DAP_SEND_OLB_TOO_LARGE;             /* permanent: data exceeds OLB capacity */
    uint64_t l_head = atomic_load_explicit(&a_q->head_pos, memory_order_acquire);
    if ((size_t)l_head < a_q->write_end)
        return DAP_SEND_OLB_FULL;                   /* transient: reader hasn't caught up */
    /* Wrap-around: reset cursors to 0 and signal compaction to reader */
    a_q->write_end = 0;
    atomic_store_explicit(&a_q->tail_pos,  0, memory_order_release);
    atomic_store_explicit(&a_q->compacted, 1, memory_order_release);
do_write:
    memcpy(a_q->data + a_q->write_end, a_data, a_len);
    a_q->write_end += a_len;
    atomic_store_explicit(&a_q->tail_pos, a_q->write_end, memory_order_release);
    return DAP_SEND_OLB_OK;
}

/**
 * @brief Flush send OLB to the socket (worker thread, single reader).
 *
 * Takes a snapshot of (head_pos, tail_pos), handles compaction if the
 * writer wrapped around, then does a single send().  EINTR is retried
 * internally; EAGAIN is returned as-is for the caller to handle.
 *
 * @return Bytes sent (> 0), 0 if OLB empty, or negative errno on error.
 */
DAP_STATIC_INLINE ssize_t
dap_send_olb_flush(dap_vmqueue_olb_t *a_q, dap_fd_t a_fd)
{
    uint64_t l_head;
    size_t l_avail;
    if (!s_olb_snapshot(a_q, &l_head, &l_avail))
        return 0;
    int l_flags = 0;
#ifdef MSG_NOSIGNAL
    l_flags = MSG_NOSIGNAL;
#endif
    ssize_t l_sent;
    do {
        l_sent = send(a_fd, a_q->data + (size_t)l_head, l_avail, l_flags);
    } while (l_sent < 0 && errno == EINTR);
    if (l_sent > 0)
        dap_vmqolb_consume(a_q, (size_t)l_sent);   /* advance head_pos, free space for writer */
    return l_sent;
}
