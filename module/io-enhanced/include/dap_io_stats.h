/**
 * @file dap_io_stats.h
 * @brief Conditionally-compiled I/O statistics for workers and processors.
 *
 * Define DAP_IO_STATS (e.g. -DDAP_IO_STATS) to enable counter collection.
 * When disabled, dap_stat() compiles to nothing — zero overhead.
 *
 * Usage:
 *   dap_stat(a_w->stats, send_bytes, += l_flushed);
 *   dap_stat(a_w->stats, suspends, ++);
 *   dap_stat(a_ctx->stats, batches_ok, ++);
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* ================================================================== */
/*  Per-worker statistics                                              */
/* ================================================================== */

typedef struct dap_worker_stats {
    size_t recv_bytes;
    size_t send_bytes;
    size_t suspends;
    size_t compacts;
    size_t eof_count;
    size_t err_count;
} dap_worker_stats_t;

/* ================================================================== */
/*  Per-processor statistics                                           */
/* ================================================================== */

typedef struct dap_proc_stats {
    size_t batches_ok;
    size_t batches_deferred;
    size_t batches_stale;
    size_t callbacks;
    size_t heap_tasks;
    size_t timer_fires;
    size_t futex_sleeps;
    size_t drain_rounds;
    size_t defer_oom;
    uint64_t busy_ns;
} dap_proc_stats_t;

/* ================================================================== */
/*  Universal stat macro                                               */
/* ================================================================== */

#ifdef DAP_IO_STATS
#define dap_stat(s, field, ...) do { if (s) (s)->field __VA_ARGS__; } while(0)
#else
#define dap_stat(s, field, ...)      ((void)0)
#endif
