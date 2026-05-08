/*
 * test_helpers_enh.h — Shared helpers for enhanced module test suite.
 *
 * Runtime: @ref dap_io.h + @ref dap_io_ops.h (topology + conn open / timer cancel).
 * For @c batch_cb / @c dap_proc_post benches include @ref dap_io_advanced.h in the .c.
 */
#pragma once

#include "dap_io.h"
#include "dap_io_ops.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

/* ================================================================== */
/*  PRNG                                                               */
/* ================================================================== */

static inline uint32_t s_xorshift32(uint32_t *a_state)
{
    uint32_t x = *a_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return *a_state = x;
}

/* ================================================================== */
/*  Frame protocol                                                     */
/* ================================================================== */

#define TEST_SIG_SIZE 8
static const uint8_t c_test_sig[TEST_SIG_SIZE] = {
    0xa0, 0x95, 0x96, 0xa9, 0x9e, 0x5c, 0xfb, 0xfa
};

typedef struct __attribute__((packed)) {
    uint8_t  sig[TEST_SIG_SIZE];
    uint32_t size;
} test_frame_hdr_t;

#define FRAME_HDR_SIZE sizeof(test_frame_hdr_t)
static inline size_t s_frame_size(uint32_t a_pl) { return FRAME_HDR_SIZE + a_pl; }

/* ================================================================== */
/*  Frame parser state machine                                         */
/* ================================================================== */

typedef enum { PS_HEADER, PS_BODY } parse_state_t;

typedef struct {
    parse_state_t state;
    uint32_t      frame_sz;
    uint32_t      max_frame_sz;
    uint64_t      tail;
    size_t        bytes_needed;
} frame_parser_t;

static inline size_t
frame_parser_feed(frame_parser_t *a_p, const char *a_data, uint64_t a_write_end)
{
    size_t l_count = 0;
    for (;;) {
        uint64_t l_avail = a_write_end - a_p->tail;
        switch (a_p->state) {
        case PS_HEADER:
            if (l_avail < FRAME_HDR_SIZE) {
                a_p->bytes_needed = FRAME_HDR_SIZE - (size_t)l_avail;
                return l_count;
            }
            { uint32_t l_pl;
              memcpy(&l_pl, a_data + a_p->tail + TEST_SIG_SIZE, sizeof(l_pl));
              uint32_t l_fsz = (uint32_t)s_frame_size(l_pl);
              if (a_p->max_frame_sz && l_fsz > a_p->max_frame_sz) {
                  a_p->tail = a_write_end;
                  a_p->bytes_needed = 0;
                  return l_count;
              }
              a_p->frame_sz = l_fsz;
            }
            a_p->state = PS_BODY;
            /* fallthrough */
        case PS_BODY:
            if (l_avail < a_p->frame_sz) {
                a_p->bytes_needed = (size_t)(a_p->frame_sz - l_avail);
                return l_count;
            }
            a_p->bytes_needed = 0;
            a_p->tail += a_p->frame_sz;
            a_p->state = PS_HEADER;
            ++l_count;
            continue;
        }
    }
}

/* ================================================================== */
/*  Fast checksum — 4 independent accumulators, unroll×4               */
/* ================================================================== */

static inline uint64_t s_checksum_u64(const void *a_data, uint32_t a_bytes)
{
    const uint8_t *l_p = (const uint8_t *)a_data;
    size_t l_n = (size_t)a_bytes >> 3;
    uint64_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    while (l_n >= 4) {
        uint64_t v0, v1, v2, v3;
        memcpy(&v0, l_p,      8);
        memcpy(&v1, l_p +  8, 8);
        memcpy(&v2, l_p + 16, 8);
        memcpy(&v3, l_p + 24, 8);
        s0 += v0; s1 += v1; s2 += v2; s3 += v3;
        l_p += 32;
        l_n -= 4;
    }
    uint64_t l_sum = (s0 + s1) + (s2 + s3);
    while (l_n--) {
        uint64_t v;
        memcpy(&v, l_p, 8);
        l_sum += v;
        l_p += 8;
    }
    return l_sum;
}

/* ================================================================== */
/*  Frame generation                                                   */
/* ================================================================== */

static inline uint8_t *s_generate_frames(size_t a_n, size_t a_min, size_t a_max,
                                          uint32_t a_seed, size_t *a_total,
                                          uint64_t *a_sum)
{
    uint32_t r1 = a_seed, r2 = a_seed;
    size_t l_tot = 0;
    for (size_t i = 0; i < a_n; ++i) {
        uint32_t l_ps = (uint32_t)a_min;
        if (a_max > a_min) l_ps += s_xorshift32(&r1) % (uint32_t)(a_max - a_min + 1);
        l_tot += FRAME_HDR_SIZE + l_ps;
    }
    uint8_t *l_buf = (uint8_t *)malloc(l_tot);
    if (!l_buf) return NULL;
    uint32_t r3 = a_seed ^ 0xDEADBEEF;
    uint8_t *l_p = l_buf; *a_sum = 0;
    for (size_t i = 0; i < a_n; ++i) {
        uint32_t l_ps = (uint32_t)a_min;
        if (a_max > a_min) l_ps += s_xorshift32(&r2) % (uint32_t)(a_max - a_min + 1);
        test_frame_hdr_t l_h;
        memcpy(l_h.sig, c_test_sig, TEST_SIG_SIZE);
        l_h.size = l_ps;
        memcpy(l_p, &l_h, FRAME_HDR_SIZE);
        uint8_t *l_pay = l_p + FRAME_HDR_SIZE;
        for (uint32_t j = 0; j + 7 < l_ps; j += 8) {
            uint64_t l_v = ((uint64_t)s_xorshift32(&r3) << 32) | s_xorshift32(&r3);
            memcpy(l_pay + j, &l_v, 8);
        }
        *a_sum += s_checksum_u64(l_pay, l_ps);
        l_p += FRAME_HDR_SIZE + l_ps;
    }
    *a_total = l_tot;
    return l_buf;
}

/* ================================================================== */
/*  Feeder — writes pre-generated frames into fd, then closes          */
/* ================================================================== */

typedef struct { int fd; const uint8_t *src; size_t src_size; } feeder_ctx_t;

static void *s_feeder(void *a_arg)
{
    feeder_ctx_t *l_ctx = a_arg;
    const uint8_t *l_p = l_ctx->src;
    size_t l_rem = l_ctx->src_size;
    struct pollfd l_pfd = { .fd = l_ctx->fd, .events = POLLOUT };
    while (l_rem) {
        ssize_t l_wr = write(l_ctx->fd, l_p, l_rem);
        if (l_wr > 0) { l_p += l_wr; l_rem -= (size_t)l_wr; }
        else if (l_wr < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            poll(&l_pfd, 1, -1);
        else if (errno != EINTR) break;
    }
    shutdown(l_ctx->fd, SHUT_WR);
    return NULL;
}

/* ================================================================== */
/*  Sink — reads from fd until EOF, totals bytes received              */
/* ================================================================== */

typedef struct {
    int fd;
    _Atomic(uint64_t) recv_bytes;
} sink_ctx_t;

static void *s_sink_thread(void *a_arg)
{
    sink_ctx_t *l_c = a_arg;
    char l_buf[65536];
    uint64_t l_total = 0;
    struct pollfd l_pfd = { .fd = l_c->fd, .events = POLLIN };
    for (;;) {
        ssize_t l_rd = read(l_c->fd, l_buf, sizeof(l_buf));
        if (l_rd > 0) { l_total += (uint64_t)l_rd; continue; }
        if (l_rd < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            { poll(&l_pfd, 1, -1); continue; }
        break;
    }
    atomic_store_explicit(&l_c->recv_bytes, l_total, memory_order_release);
    return NULL;
}

/* ================================================================== */
/*  Processor thread — pthread wrapper for dap_proc_loop_run           */
/* ================================================================== */

__attribute__((unused))
static void *s_proc_thread(void *a_arg)
{
    dap_proc_loop_run(a_arg);
    return NULL;
}

/* ================================================================== */
/*  WFQ sender — generic producer thread (lane pre-resolved by caller) */
/* ================================================================== */

typedef struct {
    dap_vmqueue_mpsc_t *wfq;
    _Atomic(bool)      *shutdown;
    unsigned            lane;
    uint8_t             msg_type;
    uint8_t             msg_pri;
    unsigned            n_tasks;
    _Atomic(uint64_t)   done_count;
} wfq_sender_ctx_t;

__attribute__((unused))
static void *s_wfq_sender(void *a_arg)
{
    wfq_sender_ctx_t *l_ctx = (wfq_sender_ctx_t *)a_arg;
    uint64_t l_count = 0;
    for (unsigned i = 0; i < l_ctx->n_tasks; ++i) {
        if (atomic_load_explicit(l_ctx->shutdown, memory_order_relaxed))
            break;
        uint64_t l_seq = i;
        if (!dap_wfq_post(l_ctx->wfq, l_ctx->lane,
                           l_ctx->msg_type, l_ctx->msg_pri,
                           &l_seq, sizeof(l_seq)))
            break;
        ++l_count;
    }
    atomic_store_explicit(&l_ctx->done_count, l_count, memory_order_release);
    return NULL;
}
