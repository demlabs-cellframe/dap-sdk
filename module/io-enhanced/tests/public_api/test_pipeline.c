/*
 * test_pipeline.c — End-to-end pipeline benchmark (zero-copy OLB -> WFQ)
 *
 *   N feeders --(socket)--> N workers (epoll+OLB+WFQ) --> 1 processor
 *
 *   Test provides only benchmark-specific callbacks:
 *     - Frame parsing (checksum, counting) / stream byte slicing
 *     - Batch processing (stats + echo verification)
 *     - Broadcast fan-out scenario
 */
#include "test_helpers_enh.h"
#include "dap_io_advanced.h"
#include "dap_io_send.h"
#include "dap_send_olb.h"

#define TIMER_50MS_US   50000
#define TIMER_200MS_US  200000
#define TIMER_PROC_US   500000

static inline uint8_t *s_generate_raw_bytes(size_t a_total, uint32_t a_seed,
                                             uint64_t *a_sum);


/* ================================================================== */
/*  Frame-based protocol extension (malloc'd, stored in conn->ext)     */
/* ================================================================== */

typedef struct {
    dap_io_olb_ext_t   olb;
    dap_io_olb_parser_t olb_parser;
    frame_parser_t     parser;
    size_t             max_frame;
} bench_ext_t;

/* ================================================================== */
/*  Stream protocol extension (fixed-chunk byte slicing)               */
/* ================================================================== */

typedef struct {
    dap_io_olb_ext_t   olb;
    dap_io_olb_parser_t olb_parser;
    uint32_t           chunk_size;
} stream_ext_t;

/* ================================================================== */
/*  Worker — parse callbacks + thin read wrappers                      */
/* ================================================================== */

static size_t s_frame_parse(dap_conn_t *a_conn, dap_io_olb_parser_t *a_p)
{
    bench_ext_t *ext = a_conn->ext;
    ext->parser.tail = a_p->tail;
    size_t l_count = frame_parser_feed(&ext->parser, a_conn->olb->data,
                                        a_conn->olb->write_end);
    a_p->tail = ext->parser.tail;
    a_conn->olb->bytes_needed = (a_conn->olb->write_end > ext->parser.tail)
                               ? ext->parser.bytes_needed : 0;
    return l_count;
}

static void s_frame_compact(dap_conn_t *a_conn)
{
    bench_ext_t *ext = a_conn->ext;
    uint32_t mf = ext->parser.max_frame_sz;
    ext->parser = (frame_parser_t){ .max_frame_sz = mf };
}

/* ================================================================== */
/*  Stream read: fixed-chunk slicing of raw byte stream                */
/* ================================================================== */

static size_t s_stream_parse(dap_conn_t *a_conn, dap_io_olb_parser_t *a_p)
{
    stream_ext_t *ext = a_conn->ext;
    uint64_t next = a_p->tail + ext->chunk_size;
    if (a_conn->olb->write_end < next)
        return 0;
    while (next + ext->chunk_size <= a_conn->olb->write_end)
        next += ext->chunk_size;
    uint64_t old = a_p->tail;
    a_p->tail = next;
    return (size_t)(next - old) / ext->chunk_size;
}

/* ================================================================== */
/*  SYNC-mode callbacks: worker-inline counting (no processor)          */
/* ================================================================== */

typedef struct {
    dap_io_olb_ext_t   olb;
    dap_io_olb_parser_t olb_parser;
    frame_parser_t     parser;
    size_t             max_frame;
    _Atomic(size_t)    frame_count;
    _Atomic(uint64_t)  frame_checksum;
} sync_frame_ext_t;

static size_t s_sync_frame_parse(dap_conn_t *a_conn, dap_io_olb_parser_t *a_p)
{
    sync_frame_ext_t *ext = a_conn->ext;
    ext->parser.tail = a_p->tail;
    uint64_t l_old = ext->parser.tail;
    size_t l_count = frame_parser_feed(&ext->parser, a_conn->olb->data,
                                        a_conn->olb->write_end);
    a_p->tail = ext->parser.tail;
    if (l_count) {
        const char *l_p = a_conn->olb->data + l_old;
        const char *l_end = a_conn->olb->data + ext->parser.tail;
        uint64_t l_cksum = 0;
        while (l_p + FRAME_HDR_SIZE <= l_end) {
            test_frame_hdr_t *l_fh = (test_frame_hdr_t *)l_p;
            size_t l_fsz = s_frame_size(l_fh->size);
            if (l_p + l_fsz > l_end) break;
            l_cksum += s_checksum_u64(l_p + FRAME_HDR_SIZE, l_fh->size);
            l_p += l_fsz;
        }
        atomic_fetch_add_explicit(&ext->frame_count, l_count, memory_order_relaxed);
        atomic_fetch_add_explicit(&ext->frame_checksum, l_cksum, memory_order_relaxed);
    }
    a_conn->olb->bytes_needed = (a_conn->olb->write_end > ext->parser.tail)
                               ? ext->parser.bytes_needed : 0;
    return l_count;
}

static void s_sync_frame_compact(dap_conn_t *a_conn)
{
    sync_frame_ext_t *ext = a_conn->ext;
    uint32_t mf = ext->parser.max_frame_sz;
    ext->parser = (frame_parser_t){ .max_frame_sz = mf };
}

typedef struct {
    dap_io_olb_ext_t   olb;
    dap_io_olb_parser_t olb_parser;
    uint32_t           chunk_size;
    _Atomic(uint64_t)  byte_count;
    _Atomic(uint64_t)  checksum;
} sync_stream_ext_t;

static size_t s_sync_stream_parse(dap_conn_t *a_conn, dap_io_olb_parser_t *a_p)
{
    sync_stream_ext_t *ext = a_conn->ext;
    uint64_t next = a_p->tail + ext->chunk_size;
    if (a_conn->olb->write_end < next)
        return 0;
    while (next + ext->chunk_size <= a_conn->olb->write_end)
        next += ext->chunk_size;
    uint64_t old = a_p->tail;
    a_p->tail = next;
    size_t cnt = (size_t)(next - old) / ext->chunk_size;
    atomic_fetch_add_explicit(&ext->byte_count, next - old, memory_order_relaxed);
    atomic_fetch_add_explicit(&ext->checksum,
        s_checksum_u64(a_conn->olb->data + old, (uint32_t)(next - old)),
        memory_order_relaxed);
    return cnt;
}

static void *s_worker_thread(void *a_arg)
{
    dap_worker_loop(a_arg);
    return NULL;
}

/* ================================================================== */
/*  Processor — batch callback + WFQ dispatch                          */
/* ================================================================== */

typedef struct {
    _Atomic(size_t)   frame_count;
    _Atomic(uint64_t) frame_bytes;
    _Atomic(uint64_t) frame_checksum;
    _Atomic(size_t)   timer_count;
    _Atomic(size_t)   ext_count;
    _Atomic(size_t)   send_count;
    _Atomic(uint64_t) send_bytes;
} proc_bench_t;

static void s_batch_cb(dap_conn_t *a_c, const char *a_batch, uint32_t a_bytes,
                        void *a_arg)
{
    (void)a_c;
    proc_bench_t *l_b = a_arg;
    atomic_fetch_add_explicit(&l_b->send_bytes, a_bytes, memory_order_relaxed);
    atomic_fetch_add_explicit(&l_b->send_count, 1, memory_order_relaxed);
    const char *l_p = a_batch, *l_end = l_p + a_bytes;
    while (l_p + FRAME_HDR_SIZE <= l_end) {
        test_frame_hdr_t *l_fh = (test_frame_hdr_t *)l_p;
        size_t l_fsz = s_frame_size(l_fh->size);
        if (l_p + l_fsz > l_end) break;
        atomic_fetch_add_explicit(&l_b->frame_checksum,
                                  s_checksum_u64(l_p + FRAME_HDR_SIZE, l_fh->size),
                                  memory_order_relaxed);
        l_p += l_fsz;
        atomic_fetch_add_explicit(&l_b->frame_count, 1, memory_order_relaxed);
    }
    atomic_fetch_add_explicit(&l_b->frame_bytes, a_bytes, memory_order_relaxed);
}

static dap_msg_rc_t s_bench_echo_frame_rc(dap_conn_t *a_c, const char *a_batch, uint32_t a_bytes,
                                          void *a_arg)
{
    dap_send_olb_result_t l_wr = dap_send_olb_write(a_c->send_olb, a_batch, a_bytes);
    if (l_wr == DAP_SEND_OLB_FULL) {
        dap_worker_conn_notify_send(a_c);
        return DAP_MSG_DEFER;
    }
    if (l_wr != DAP_SEND_OLB_OK)
        return DAP_MSG_DROP;
    s_batch_cb(a_c, a_batch, a_bytes, a_arg);
    dap_worker_conn_notify_send(a_c);
    return DAP_MSG_DONE;
}

static dap_msg_rc_t s_bench_echo_frame_batch_cb(const dap_batch_task_t *a_task, void *a_arg)
{
    return dap_proc_exec_batch_rc(a_task, dap_tls_proc && dap_tls_proc->force_complete,
                                  s_bench_echo_frame_rc, a_arg);
}

static void s_timer_tick(void *a_arg)
{
    proc_bench_t *l_b = (proc_bench_t *)a_arg;
    atomic_fetch_add_explicit(&l_b->timer_count, 1, memory_order_relaxed);
}

static dap_msg_rc_t s_proc_heap_cb(const dap_heap_task_t *a_task, void *a_arg)
{
    (void)a_task;
    proc_bench_t *l_b = (proc_bench_t *)a_arg;
    atomic_fetch_add_explicit(&l_b->ext_count, 1, memory_order_relaxed);
    return DAP_MSG_DONE;
}

/* ================================================================== */
/*  EXT sender — typed heap push via Treiber message stack              */
/* ================================================================== */

typedef struct {
    dap_proc_ctx_t     *proc;
    _Atomic(bool)      *shutdown;
    unsigned            n_tasks;
    _Atomic(uint64_t)   done_count;
} ext_sender_ctx_t;

static void *s_ext_heap_sender(void *a_arg)
{
    ext_sender_ctx_t *l_ctx = a_arg;
    uint64_t l_count = 0;
    for (unsigned i = 0; i < l_ctx->n_tasks; ++i) {
        if (atomic_load_explicit(l_ctx->shutdown, memory_order_relaxed))
            break;
        if (!dap_msg_post_heap(l_ctx->proc, NULL, 0, NULL))
            break;
        ++l_count;
    }
    atomic_store_explicit(&l_ctx->done_count, l_count, memory_order_release);
    return NULL;
}

/* ================================================================== */
/*  Benchmark runner                                                   */
/* ================================================================== */

#define OLB_CAPACITY      Mbytes(2)

static void s_bench_timer_cb(void *a_arg)
{
    ++*(size_t *)a_arg;
}

/**
 * @brief Unified frame pipeline benchmark.
 *
 * @param a_nw      Number of worker threads.
 * @param a_nc      Number of connections (each with its own socket pair).
 * @param a_nframes Frames per connection.
 * @param a_min     Minimum payload size (bytes).
 * @param a_max     Maximum payload size (bytes).
 *
 * Topology: connections are assigned round-robin to workers (conn i -> worker i%nw).
 *   a_nw == a_nc  -> dedicated: 1 conn per worker, SPSC lane isolation
 *   a_nw <  a_nc  -> shared:    N conns multiplex 1 worker, OLB + pending_bits stress
 *
 * Data path: socket -> OLB -> worker parse -> WFQ NORM -> proc batch_cb (test echo) -> send_olb
 *
 * Correctness checks: frame count, payload checksum, sink byte count.
 */
static void s_bench(unsigned a_nw, unsigned a_nc, size_t a_nframes,
                    size_t a_min, size_t a_max)
{
    dap_io_t *l_io = dap_io_create(a_nw, 1);
    if (!l_io) { printf("  %uFx%uWx1P SKIP\n", a_nc, a_nw); return; }

    feeder_ctx_t       *l_feeders = calloc(a_nc, sizeof(*l_feeders));
    dap_conn_t        **l_conns   = calloc(a_nc, sizeof(*l_conns));
    bench_ext_t        *l_exts    = calloc(a_nc, sizeof(*l_exts));
    uint8_t           **l_srcs    = calloc(a_nc, sizeof(*l_srcs));
    size_t             *l_src_sz  = calloc(a_nc, sizeof(*l_src_sz));
    uint64_t           *l_sums    = calloc(a_nc, sizeof(*l_sums));
    pthread_t          *l_fts     = calloc(a_nc, sizeof(pthread_t));
    pthread_t          *l_wts     = calloc(a_nw, sizeof(pthread_t));
    int (*l_socks)[2]             = malloc(a_nc * sizeof(*l_socks));
    sink_ctx_t         *l_sinks   = calloc(a_nc, sizeof(*l_sinks));
    pthread_t          *l_sts     = calloc(a_nc, sizeof(pthread_t));

    proc_bench_t l_bench = {0};

    for (unsigned i = 0; i < a_nc; ++i)
        l_socks[i][0] = l_socks[i][1] = -1;

    for (unsigned i = 0; i < a_nc; ++i) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, l_socks[i]) < 0)
            { printf("  %uFx%uWx1P SKIP (sock)\n", a_nc, a_nw); goto done; }
        fcntl(l_socks[i][0], F_SETFL, fcntl(l_socks[i][0], F_GETFL) | O_NONBLOCK);
        fcntl(l_socks[i][1], F_SETFL, fcntl(l_socks[i][1], F_GETFL) | O_NONBLOCK);

        l_srcs[i] = s_generate_frames(a_nframes, a_min, a_max,
                                       42u + i, &l_src_sz[i], &l_sums[i]);
        if (!l_srcs[i]) { printf("  %uFx%uWx1P SKIP (gen)\n", a_nc, a_nw); goto done; }

        unsigned l_wid = i % a_nw;
        size_t l_mf = s_frame_size((uint32_t)a_max);
        l_exts[i] = (bench_ext_t){
            .parser = { .max_frame_sz = (uint32_t)l_mf }, .max_frame = l_mf };
        (void)dap_io_olb_ext_setup(&l_exts[i].olb, &l_exts[i].olb_parser,
            s_frame_parse, s_frame_compact);
        l_conns[i] = dap_io_conn_open(l_io, l_wid, DAP_IO_SOCK, l_socks[i][0],
                                       OLB_CAPACITY, NULL, dap_io_rx_bridge, &l_exts[i], l_mf);
        if (!l_conns[i]) { printf("  %uFx%uWx1P SKIP (conn)\n", a_nc, a_nw); goto done; }

        l_feeders[i] = (feeder_ctx_t){
            .fd = l_socks[i][1], .src = l_srcs[i], .src_size = l_src_sz[i] };
    }

    uint64_t l_expected_sum = 0;
    for (unsigned i = 0; i < a_nc; ++i)
        l_expected_sum += l_sums[i];

    dap_proc_ctx_t *l_pctx = dap_io_proc(l_io, 0);
    (void)dap_io_proc_set_batch_cb(l_io, 0, s_bench_echo_frame_batch_cb, &l_bench);

    size_t l_total_frames = a_nc * a_nframes;
    pthread_t l_pth;
    uint64_t l_t0 = dap_nanotime_now();

    for (unsigned i = 0; i < a_nc; ++i) {
        l_sinks[i] = (sink_ctx_t){ .fd = l_socks[i][1] };
        pthread_create(&l_sts[i], NULL, s_sink_thread, &l_sinks[i]);
    }
    pthread_create(&l_pth, NULL, s_proc_thread, l_pctx);
    for (unsigned i = 0; i < a_nw; ++i)
        pthread_create(&l_wts[i], NULL, s_worker_thread, dap_io_worker(l_io, i));
    for (unsigned i = 0; i < a_nc; ++i)
        pthread_create(&l_fts[i], NULL, s_feeder, &l_feeders[i]);

    for (unsigned i = 0; i < a_nc; ++i) pthread_join(l_fts[i], NULL);
    while (atomic_load_explicit(&l_bench.frame_count, memory_order_acquire) < l_total_frames)
        sched_yield();

    dap_io_shutdown(l_io);
    pthread_join(l_pth, NULL);

    for (unsigned i = 0; i < a_nw; ++i)
        dap_worker_request_stop(dap_io_worker(l_io, i));
    for (unsigned i = 0; i < a_nw; ++i) pthread_join(l_wts[i], NULL);

    size_t l_final_flush = 0;
    for (bool l_more = true; l_more; ) {
        l_more = false;
        for (unsigned i = 0; i < a_nc; ++i) {
            if (!l_conns[i] || !l_conns[i]->send_olb || l_socks[i][0] < 0)
                continue;
            ssize_t l_f = dap_send_olb_flush(l_conns[i]->send_olb, l_socks[i][0]);
            if (l_f > 0) { l_final_flush += (size_t)l_f; l_more = true; }
            else if (l_f < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                l_more = true;
        }
    }
    for (unsigned i = 0; i < a_nc; ++i) {
        close(l_socks[i][0]);
        l_socks[i][0] = -1;
    }
    for (unsigned i = 0; i < a_nc; ++i)
        pthread_join(l_sts[i], NULL);

    uint64_t l_elapsed = dap_nanotime_now() - l_t0;
    uint64_t l_total_bytes = 0;
    for (unsigned i = 0; i < a_nc; ++i)
        l_total_bytes += l_src_sz[i];

    size_t l_total_sus = 0, l_total_cmp = 0, l_total_mmv = 0, l_total_early = 0, l_total_send = 0;
#ifdef DAP_IO_STATS
    for (unsigned i = 0; i < a_nw; ++i) {
        l_total_sus  += l_io->worker_stats[i].suspends;
        l_total_cmp  += l_io->worker_stats[i].compacts;
        l_total_send += l_io->worker_stats[i].send_bytes;
    }
#endif
    for (unsigned i = 0; i < a_nc; ++i) {
        if (l_conns[i] && l_conns[i]->olb) {
            l_total_mmv   += l_conns[i]->olb->memmove_compacts;
            l_total_early += l_conns[i]->olb->early_compacts;
        }
    }

    double l_sec = (double)l_elapsed / 1e9;
    size_t l_frame_count = atomic_load_explicit(&l_bench.frame_count, memory_order_relaxed);
    uint64_t l_frame_checksum = atomic_load_explicit(&l_bench.frame_checksum, memory_order_relaxed);
    size_t l_send_count = atomic_load_explicit(&l_bench.send_count, memory_order_relaxed);
    bool l_frames_ok = (l_frame_count == l_total_frames);
    uint64_t l_sink_bytes = 0;
    for (unsigned i = 0; i < a_nc; ++i)
        l_sink_bytes += atomic_load_explicit(&l_sinks[i].recv_bytes, memory_order_acquire);
    bool l_chk_ok = (l_frame_checksum == l_expected_sum);
    uint64_t l_flushed_total = l_total_send + l_final_flush;
    bool l_sink_ok = (l_sink_bytes == l_flushed_total);
    printf("  %uFx%uWx1P: %6.2f Mfps  %5.2f Gbps  frames:%zu"
           "  sus %zu cmp %zu(early %zu mmv %zu) echo %zu",
           a_nc, a_nw,
           (double)l_total_frames / l_sec / 1e6,
           (double)l_total_bytes * 8.0 / l_sec / 1e9,
           l_frame_count,
           l_total_sus, l_total_cmp, l_total_early, l_total_mmv,
           l_send_count);
#ifdef DAP_IO_STATS
    { dap_proc_stats_t *l_ps = &l_io->proc_stats[0];
      printf(" def %zu stale %zu tmr %zu busy %.0f%%",
             l_ps->batches_deferred, l_ps->batches_stale, l_ps->timer_fires,
             l_elapsed ? (double)l_ps->busy_ns / (double)l_elapsed * 100.0 : 0.0);
      if (l_ps->defer_oom) printf(" oom %zu", l_ps->defer_oom);
    }
#endif
    printf("  %s\n",
           (l_frames_ok && l_chk_ok && l_sink_ok) ? "OK" : "MISMATCH");
    if (!l_frames_ok)
        printf("    !! frames: got %zu expected %zu\n", l_frame_count, l_total_frames);
    if (!l_chk_ok)
        printf("    !! checksum: got %llu expected %llu\n",
               (unsigned long long)l_frame_checksum, (unsigned long long)l_expected_sum);
    if (!l_sink_ok)
        printf("    !! sink: got %llu  flushed_total %llu  w_flush %zu  fin_flush %zu  (delta %lld)\n",
               (unsigned long long)l_sink_bytes, (unsigned long long)l_flushed_total,
               l_total_send, l_final_flush,
               (long long)((int64_t)l_sink_bytes - (int64_t)l_flushed_total));
done:
    for (unsigned i = 0; i < a_nc; ++i) {
        if (l_srcs && l_srcs[i]) free(l_srcs[i]);
        if (l_socks[i][0] >= 0) close(l_socks[i][0]);
        if (l_socks[i][1] >= 0) close(l_socks[i][1]);
    }
    free(l_feeders); free(l_conns);
    free(l_exts);
    free(l_srcs); free(l_src_sz); free(l_sums);
    free(l_fts); free(l_wts);
    free(l_socks); free(l_sinks); free(l_sts);
    dap_io_destroy(l_io);
}

/* ================================================================== */
/*  SYNC frame benchmark: worker-inline, no processor data path        */
/* ================================================================== */

static void s_bench_sync(unsigned a_nw, unsigned a_nc, size_t a_nframes,
                          size_t a_min, size_t a_max)
{
    dap_io_t *l_io = dap_io_create(a_nw, 1);
    if (!l_io) { printf("  %uFx%uW SYNC SKIP\n", a_nc, a_nw); return; }

    feeder_ctx_t        *l_feeders = calloc(a_nc, sizeof(*l_feeders));
    dap_conn_t         **l_conns   = calloc(a_nc, sizeof(*l_conns));
    sync_frame_ext_t    *l_exts    = calloc(a_nc, sizeof(*l_exts));
    uint8_t            **l_srcs    = calloc(a_nc, sizeof(*l_srcs));
    size_t              *l_src_sz  = calloc(a_nc, sizeof(*l_src_sz));
    uint64_t            *l_sums    = calloc(a_nc, sizeof(*l_sums));
    pthread_t           *l_fts     = calloc(a_nc, sizeof(pthread_t));
    pthread_t           *l_wts     = calloc(a_nw, sizeof(pthread_t));
    int (*l_socks)[2]              = malloc(a_nc * sizeof(*l_socks));

    for (unsigned i = 0; i < a_nc; ++i)
        l_socks[i][0] = l_socks[i][1] = -1;

    for (unsigned i = 0; i < a_nc; ++i) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, l_socks[i]) < 0)
            { printf("  %uFx%uW SYNC SKIP (sock)\n", a_nc, a_nw); goto done; }
        fcntl(l_socks[i][0], F_SETFL, fcntl(l_socks[i][0], F_GETFL) | O_NONBLOCK);
        fcntl(l_socks[i][1], F_SETFL, fcntl(l_socks[i][1], F_GETFL) | O_NONBLOCK);

        l_srcs[i] = s_generate_frames(a_nframes, a_min, a_max,
                                       42u + i, &l_src_sz[i], &l_sums[i]);
        if (!l_srcs[i]) { printf("  %uFx%uW SYNC SKIP (gen)\n", a_nc, a_nw); goto done; }

        unsigned l_wid = i % a_nw;
        size_t l_mf = s_frame_size((uint32_t)a_max);
        l_exts[i] = (sync_frame_ext_t){
            .parser = { .max_frame_sz = (uint32_t)l_mf }, .max_frame = l_mf };
        (void)dap_io_olb_ext_setup(&l_exts[i].olb, &l_exts[i].olb_parser,
            s_sync_frame_parse, s_sync_frame_compact);
        l_conns[i] = dap_io_conn_open(l_io, l_wid, DAP_IO_SOCK, l_socks[i][0],
                                       OLB_CAPACITY, NULL, dap_io_rx_bridge, &l_exts[i], l_mf);
        if (!l_conns[i]) { printf("  %uFx%uW SYNC SKIP (conn)\n", a_nc, a_nw); goto done; }
        dap_conn_enter_sync(l_conns[i]);

        l_feeders[i] = (feeder_ctx_t){
            .fd = l_socks[i][1], .src = l_srcs[i], .src_size = l_src_sz[i] };
    }

    uint64_t l_expected_sum = 0;
    for (unsigned i = 0; i < a_nc; ++i)
        l_expected_sum += l_sums[i];

    size_t l_total_frames = a_nc * a_nframes;
    dap_proc_ctx_t *l_pctx = dap_io_proc(l_io, 0);
    pthread_t l_pth;
    uint64_t l_t0 = dap_nanotime_now();

    pthread_create(&l_pth, NULL, s_proc_thread, l_pctx);
    for (unsigned i = 0; i < a_nw; ++i)
        pthread_create(&l_wts[i], NULL, s_worker_thread, dap_io_worker(l_io, i));
    for (unsigned i = 0; i < a_nc; ++i)
        pthread_create(&l_fts[i], NULL, s_feeder, &l_feeders[i]);

    for (unsigned i = 0; i < a_nc; ++i) pthread_join(l_fts[i], NULL);

    for (;;) {
        size_t l_got = 0;
        for (unsigned i = 0; i < a_nc; ++i)
            l_got += atomic_load_explicit(&l_exts[i].frame_count, memory_order_acquire);
        if (l_got >= l_total_frames) break;
        sched_yield();
    }

    dap_io_shutdown(l_io);
    pthread_join(l_pth, NULL);
    for (unsigned i = 0; i < a_nw; ++i)
        dap_worker_request_stop(dap_io_worker(l_io, i));
    for (unsigned i = 0; i < a_nw; ++i) pthread_join(l_wts[i], NULL);

    uint64_t l_elapsed = dap_nanotime_now() - l_t0;
    uint64_t l_total_bytes = 0;
    for (unsigned i = 0; i < a_nc; ++i) l_total_bytes += l_src_sz[i];

    size_t l_frame_count = 0;
    uint64_t l_checksum = 0;
    for (unsigned i = 0; i < a_nc; ++i) {
        l_frame_count += atomic_load_explicit(&l_exts[i].frame_count, memory_order_relaxed);
        l_checksum += atomic_load_explicit(&l_exts[i].frame_checksum, memory_order_relaxed);
    }

    size_t l_total_cmp = 0;
#ifdef DAP_IO_STATS
    for (unsigned i = 0; i < a_nw; ++i)
        l_total_cmp += l_io->worker_stats[i].compacts;
#endif

    double l_sec = (double)l_elapsed / 1e9;
    bool l_ok = (l_frame_count == l_total_frames) && (l_checksum == l_expected_sum);
    printf("  %uFx%uWxSYNC: %6.2f Mfps  %5.2f Gbps  frames:%zu  cmp %zu  %s\n",
           a_nc, a_nw,
           (double)l_total_frames / l_sec / 1e6,
           (double)l_total_bytes * 8.0 / l_sec / 1e9,
           l_frame_count, l_total_cmp,
           l_ok ? "OK" : "MISMATCH");
    if (l_frame_count != l_total_frames)
        printf("    !! frames: got %zu expected %zu\n", l_frame_count, l_total_frames);
    if (l_checksum != l_expected_sum)
        printf("    !! checksum: got %llu expected %llu\n",
               (unsigned long long)l_checksum, (unsigned long long)l_expected_sum);
done:
    for (unsigned i = 0; i < a_nc; ++i) {
        if (l_srcs && l_srcs[i]) free(l_srcs[i]);
        if (l_socks[i][0] >= 0) close(l_socks[i][0]);
        if (l_socks[i][1] >= 0) close(l_socks[i][1]);
    }
    free(l_feeders); free(l_conns); free(l_exts);
    free(l_srcs); free(l_src_sz); free(l_sums);
    free(l_fts); free(l_wts); free(l_socks);
    dap_io_destroy(l_io);
}

/* ================================================================== */
/*  SYNC stream benchmark: worker-inline, no processor data path       */
/* ================================================================== */

static void s_bench_stream_sync(unsigned a_nw, unsigned a_nc,
                                 size_t a_total_per_conn, uint32_t a_chunk_size)
{
    dap_io_t *l_io = dap_io_create(a_nw, 1);
    if (!l_io) { printf("  %uFx%uW SYNC SKIP\n", a_nc, a_nw); return; }

    size_t l_total = (a_total_per_conn / a_chunk_size) * a_chunk_size;

    feeder_ctx_t        *l_feeders = calloc(a_nc, sizeof(*l_feeders));
    dap_conn_t         **l_conns   = calloc(a_nc, sizeof(*l_conns));
    sync_stream_ext_t   *l_exts    = calloc(a_nc, sizeof(*l_exts));
    uint8_t            **l_srcs    = calloc(a_nc, sizeof(*l_srcs));
    uint64_t            *l_sums    = calloc(a_nc, sizeof(*l_sums));
    pthread_t           *l_fts     = calloc(a_nc, sizeof(pthread_t));
    pthread_t           *l_wts     = calloc(a_nw, sizeof(pthread_t));
    int (*l_socks)[2]              = malloc(a_nc * sizeof(*l_socks));

    for (unsigned i = 0; i < a_nc; ++i)
        l_socks[i][0] = l_socks[i][1] = -1;

    for (unsigned i = 0; i < a_nc; ++i) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, l_socks[i]) < 0)
            { printf("  %uFx%uW SYNC SKIP (sock)\n", a_nc, a_nw); goto done; }
        fcntl(l_socks[i][0], F_SETFL, fcntl(l_socks[i][0], F_GETFL) | O_NONBLOCK);
        fcntl(l_socks[i][1], F_SETFL, fcntl(l_socks[i][1], F_GETFL) | O_NONBLOCK);

        l_srcs[i] = s_generate_raw_bytes(l_total, 42u + i, &l_sums[i]);
        if (!l_srcs[i]) { printf("  %uFx%uW SYNC SKIP (gen)\n", a_nc, a_nw); goto done; }

        unsigned l_wid = i % a_nw;
        l_exts[i] = (sync_stream_ext_t){ .chunk_size = a_chunk_size };
        (void)dap_io_olb_ext_setup(&l_exts[i].olb, &l_exts[i].olb_parser,
            s_sync_stream_parse, NULL);
        l_conns[i] = dap_io_conn_open(l_io, l_wid, DAP_IO_SOCK, l_socks[i][0],
                                       OLB_CAPACITY, NULL, dap_io_rx_bridge, &l_exts[i], 0);
        if (!l_conns[i]) { printf("  %uFx%uW SYNC SKIP (conn)\n", a_nc, a_nw); goto done; }
        dap_conn_enter_sync(l_conns[i]);

        l_feeders[i] = (feeder_ctx_t){
            .fd = l_socks[i][1], .src = l_srcs[i], .src_size = l_total };
    }

    uint64_t l_expected_sum = 0;
    for (unsigned i = 0; i < a_nc; ++i) l_expected_sum += l_sums[i];
    uint64_t l_expected_bytes = (uint64_t)a_nc * l_total;

    dap_proc_ctx_t *l_pctx = dap_io_proc(l_io, 0);
    pthread_t l_pth;
    uint64_t l_t0 = dap_nanotime_now();

    pthread_create(&l_pth, NULL, s_proc_thread, l_pctx);
    for (unsigned i = 0; i < a_nw; ++i)
        pthread_create(&l_wts[i], NULL, s_worker_thread, dap_io_worker(l_io, i));
    for (unsigned i = 0; i < a_nc; ++i)
        pthread_create(&l_fts[i], NULL, s_feeder, &l_feeders[i]);

    for (unsigned i = 0; i < a_nc; ++i) pthread_join(l_fts[i], NULL);

    for (;;) {
        uint64_t l_got = 0;
        for (unsigned i = 0; i < a_nc; ++i)
            l_got += atomic_load_explicit(&l_exts[i].byte_count, memory_order_acquire);
        if (l_got >= l_expected_bytes) break;
        sched_yield();
    }

    dap_io_shutdown(l_io);
    pthread_join(l_pth, NULL);
    for (unsigned i = 0; i < a_nw; ++i)
        dap_worker_request_stop(dap_io_worker(l_io, i));
    for (unsigned i = 0; i < a_nw; ++i) pthread_join(l_wts[i], NULL);

    uint64_t l_elapsed = dap_nanotime_now() - l_t0;

    uint64_t l_byte_count = 0, l_checksum = 0;
    for (unsigned i = 0; i < a_nc; ++i) {
        l_byte_count += atomic_load_explicit(&l_exts[i].byte_count, memory_order_relaxed);
        l_checksum += atomic_load_explicit(&l_exts[i].checksum, memory_order_relaxed);
    }

    double l_sec = (double)l_elapsed / 1e9;
    bool l_ok = (l_byte_count == l_expected_bytes) && (l_checksum == l_expected_sum);
    printf("  %uFx%uWxSYNC: %6.2f Gbps  bytes:%llu/%llu  %s\n",
           a_nc, a_nw,
           (double)l_expected_bytes * 8.0 / l_sec / 1e9,
           (unsigned long long)l_byte_count, (unsigned long long)l_expected_bytes,
           l_ok ? "OK" : "MISMATCH");
    if (l_checksum != l_expected_sum)
        printf("    !! checksum: got %llu expected %llu\n",
               (unsigned long long)l_checksum, (unsigned long long)l_expected_sum);
done:
    for (unsigned i = 0; i < a_nc; ++i) {
        if (l_srcs && l_srcs[i]) free(l_srcs[i]);
        if (l_socks[i][0] >= 0) close(l_socks[i][0]);
        if (l_socks[i][1] >= 0) close(l_socks[i][1]);
    }
    free(l_feeders); free(l_conns); free(l_exts);
    free(l_srcs); free(l_sums);
    free(l_fts); free(l_wts); free(l_socks);
    dap_io_destroy(l_io);
}

/* ================================================================== */
/*  Mode-switch benchmark: SYNC → ASYNC → SYNC with byte thresholds   */
/*                                                                     */
/*  Demonstrates runtime switching from a user's perspective:          */
/*    1. Connection starts in SYNC  (worker-inline processing)         */
/*    2. After threshold_1 bytes → dap_conn_enter_async()              */
/*    3. After threshold_2 bytes → dap_conn_enter_sync()               */
/*  The parse callback is the SAME throughout; it checks DAP_CONN_SYNC */
/*  to decide whether to count frames inline or let the processor do   */
/*  it.  Phase transitions run once per read_cb AFTER dap_io_rx_bridge */
/*  (so all parse calls inside a single dap_worker_rx_olb still share  */
/*  the same mode — same as a dedicated read_cb that only wrapped OLB). */
/* ================================================================== */

typedef enum {
    SWITCH_SYNC_1,
    SWITCH_ASYNC,
    SWITCH_SYNC_2
} switch_phase_t;

typedef struct {
    dap_io_olb_ext_t   olb;
    dap_io_olb_parser_t olb_parser;
    frame_parser_t     parser;
    size_t             max_frame;
    switch_phase_t     phase;
    uint64_t           bytes_received;
    uint64_t           threshold_1;
    uint64_t           threshold_2;
    _Atomic(size_t)    sync_frame_count;
    _Atomic(uint64_t)  sync_checksum;
} switch_ext_t;

static size_t s_switch_parse(dap_conn_t *a_conn, dap_io_olb_parser_t *a_p)
{
    switch_ext_t *ext = a_conn->ext;
    ext->parser.tail = a_p->tail;
    uint64_t l_old = ext->parser.tail;
    size_t l_count = frame_parser_feed(&ext->parser, a_conn->olb->data,
                                        a_conn->olb->write_end);
    a_p->tail = ext->parser.tail;
    if (l_count) {
        ext->bytes_received += ext->parser.tail - l_old;
        if (dap_conn_state(a_conn) & DAP_CONN_SYNC) {
            const char *l_p = a_conn->olb->data + l_old;
            const char *l_end = a_conn->olb->data + ext->parser.tail;
            uint32_t l_nbytes = (uint32_t)(l_end - l_p);
            if (l_nbytes) {
                /* Sync-phase echo: parse_fn runs on the owning worker,
                 * so we have a known-live dap_conn_t * and can skip
                 * the handle/resolve dance entirely. */
                dap_io_tx_send_direct(a_conn, l_p, l_nbytes);
            }
            uint64_t l_cksum = 0;
            while (l_p + FRAME_HDR_SIZE <= l_end) {
                test_frame_hdr_t *l_fh = (test_frame_hdr_t *)l_p;
                size_t l_fsz = s_frame_size(l_fh->size);
                if (l_p + l_fsz > l_end) break;
                l_cksum += s_checksum_u64(l_p + FRAME_HDR_SIZE, l_fh->size);
                l_p += l_fsz;
            }
            atomic_fetch_add_explicit(&ext->sync_frame_count, l_count,
                                       memory_order_relaxed);
            atomic_fetch_add_explicit(&ext->sync_checksum, l_cksum,
                                       memory_order_relaxed);
        }
    }
    a_conn->olb->bytes_needed = (a_conn->olb->write_end > ext->parser.tail)
                               ? ext->parser.bytes_needed : 0;
    return l_count;
}

static void s_switch_compact(dap_conn_t *a_conn)
{
    switch_ext_t *ext = a_conn->ext;
    uint32_t mf = ext->parser.max_frame_sz;
    ext->parser = (frame_parser_t){ .max_frame_sz = mf };
}

static void
s_switch_after_rx_olb(dap_conn_t *a_c)
{
    switch_ext_t *l_e = a_c->ext;
    switch (l_e->phase) {
    case SWITCH_SYNC_1:
        if (l_e->bytes_received >= l_e->threshold_1) {
            l_e->phase = SWITCH_ASYNC;
            dap_conn_enter_async(a_c);
        }
        break;
    case SWITCH_ASYNC:
        if (l_e->bytes_received >= l_e->threshold_2) {
            l_e->phase = SWITCH_SYNC_2;
            dap_conn_enter_sync(a_c);
        }
        break;
    case SWITCH_SYNC_2:
        break;
    }
}

static void s_switch_read(dap_conn_t *a_c)
{
    dap_io_rx_bridge(a_c);
    s_switch_after_rx_olb(a_c);
}

static void s_bench_mode_switch(unsigned a_nw, unsigned a_nc, size_t a_nframes,
                                 size_t a_min, size_t a_max)
{
    dap_io_t *l_io = dap_io_create(a_nw, 1);
    if (!l_io) { printf("  %uFx%uW SWITCH SKIP\n", a_nc, a_nw); return; }

    feeder_ctx_t     *l_feeders = calloc(a_nc, sizeof(*l_feeders));
    dap_conn_t      **l_conns   = calloc(a_nc, sizeof(*l_conns));
    switch_ext_t     *l_exts    = calloc(a_nc, sizeof(*l_exts));
    uint8_t         **l_srcs    = calloc(a_nc, sizeof(*l_srcs));
    size_t           *l_src_sz  = calloc(a_nc, sizeof(*l_src_sz));
    uint64_t         *l_sums    = calloc(a_nc, sizeof(*l_sums));
    pthread_t        *l_fts     = calloc(a_nc, sizeof(pthread_t));
    pthread_t        *l_wts     = calloc(a_nw, sizeof(pthread_t));
    int (*l_socks)[2]           = malloc(a_nc * sizeof(*l_socks));
    sink_ctx_t       *l_sinks   = calloc(a_nc, sizeof(*l_sinks));
    pthread_t        *l_sts     = calloc(a_nc, sizeof(pthread_t));

    proc_bench_t l_bench = {0};

    for (unsigned i = 0; i < a_nc; ++i)
        l_socks[i][0] = l_socks[i][1] = -1;

    for (unsigned i = 0; i < a_nc; ++i) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, l_socks[i]) < 0)
            { printf("  %uFx%uW SWITCH SKIP (sock)\n", a_nc, a_nw); goto done; }
        fcntl(l_socks[i][0], F_SETFL, fcntl(l_socks[i][0], F_GETFL) | O_NONBLOCK);
        fcntl(l_socks[i][1], F_SETFL, fcntl(l_socks[i][1], F_GETFL) | O_NONBLOCK);

        l_srcs[i] = s_generate_frames(a_nframes, a_min, a_max,
                                       42u + i, &l_src_sz[i], &l_sums[i]);
        if (!l_srcs[i]) { printf("  %uFx%uW SWITCH SKIP (gen)\n", a_nc, a_nw); goto done; }

        unsigned l_wid = i % a_nw;
        size_t l_mf = s_frame_size((uint32_t)a_max);
        l_exts[i] = (switch_ext_t){
            .parser = { .max_frame_sz = (uint32_t)l_mf },
            .max_frame = l_mf,
            .phase = SWITCH_SYNC_1,
            .threshold_1 = l_src_sz[i] / 3,
            .threshold_2 = l_src_sz[i] * 2 / 3 };
        (void)dap_io_olb_ext_setup(&l_exts[i].olb, &l_exts[i].olb_parser,
            s_switch_parse, s_switch_compact);
        l_conns[i] = dap_io_conn_open(l_io, l_wid, DAP_IO_SOCK, l_socks[i][0],
                                       OLB_CAPACITY, NULL, s_switch_read, &l_exts[i], l_mf);
        if (!l_conns[i]) { printf("  %uFx%uW SWITCH SKIP (conn)\n", a_nc, a_nw); goto done; }
        dap_conn_enter_sync(l_conns[i]);

        l_feeders[i] = (feeder_ctx_t){
            .fd = l_socks[i][1], .src = l_srcs[i], .src_size = l_src_sz[i] };
    }

    uint64_t l_expected_sum = 0;
    for (unsigned i = 0; i < a_nc; ++i)
        l_expected_sum += l_sums[i];

    size_t l_total_frames = a_nc * a_nframes;

    dap_proc_ctx_t *l_pctx = dap_io_proc(l_io, 0);
    (void)dap_io_proc_set_batch_cb(l_io, 0, s_bench_echo_frame_batch_cb, &l_bench);

    pthread_t l_pth;
    uint64_t l_t0 = dap_nanotime_now();

    for (unsigned i = 0; i < a_nc; ++i) {
        l_sinks[i] = (sink_ctx_t){ .fd = l_socks[i][1] };
        pthread_create(&l_sts[i], NULL, s_sink_thread, &l_sinks[i]);
    }
    pthread_create(&l_pth, NULL, s_proc_thread, l_pctx);
    for (unsigned i = 0; i < a_nw; ++i)
        pthread_create(&l_wts[i], NULL, s_worker_thread, dap_io_worker(l_io, i));
    for (unsigned i = 0; i < a_nc; ++i)
        pthread_create(&l_fts[i], NULL, s_feeder, &l_feeders[i]);

    for (unsigned i = 0; i < a_nc; ++i) pthread_join(l_fts[i], NULL);

    for (;;) {
        size_t l_sync = 0;
        for (unsigned i = 0; i < a_nc; ++i)
            l_sync += atomic_load_explicit(&l_exts[i].sync_frame_count,
                                            memory_order_acquire);
        size_t l_async = atomic_load_explicit(&l_bench.frame_count, memory_order_acquire);
        if (l_sync + l_async >= l_total_frames) break;
        sched_yield();
    }

    dap_io_shutdown(l_io);
    pthread_join(l_pth, NULL);
    for (unsigned i = 0; i < a_nw; ++i)
        dap_worker_request_stop(dap_io_worker(l_io, i));
    for (unsigned i = 0; i < a_nw; ++i) pthread_join(l_wts[i], NULL);
    for (unsigned i = 0; i < a_nc; ++i)
        shutdown(l_socks[i][0], SHUT_WR);
    for (unsigned i = 0; i < a_nc; ++i) pthread_join(l_sts[i], NULL);

    uint64_t l_elapsed = dap_nanotime_now() - l_t0;
    uint64_t l_total_bytes = 0;
    for (unsigned i = 0; i < a_nc; ++i) l_total_bytes += l_src_sz[i];

    size_t l_sync_count = 0;
    uint64_t l_sync_cksum = 0;
    for (unsigned i = 0; i < a_nc; ++i) {
        l_sync_count += atomic_load_explicit(&l_exts[i].sync_frame_count,
                                              memory_order_relaxed);
        l_sync_cksum += atomic_load_explicit(&l_exts[i].sync_checksum,
                                              memory_order_relaxed);
    }
    size_t l_async_count = atomic_load_explicit(&l_bench.frame_count, memory_order_relaxed);
    uint64_t l_async_cksum = atomic_load_explicit(&l_bench.frame_checksum, memory_order_relaxed);
    size_t l_total_count = l_sync_count + l_async_count;
    uint64_t l_total_cksum = l_sync_cksum + l_async_cksum;

    double l_sec = (double)l_elapsed / 1e9;
    bool l_ok = (l_total_count == l_total_frames) && (l_total_cksum == l_expected_sum);
    printf("  %uFx%uW SWITCH: %5.2f Mfps  sync:%zuK async:%zuK  total:%zu/%zu  %s\n",
           a_nc, a_nw,
           (double)l_total_frames / l_sec / 1e6,
           l_sync_count / 1000, l_async_count / 1000,
           l_total_count, l_total_frames,
           l_ok ? "OK" : "MISMATCH");
    if (!l_ok)
        printf("    !! sync %zu + async %zu = %zu (expected %zu)  cksum %s\n",
               l_sync_count, l_async_count, l_total_count, l_total_frames,
               (l_total_cksum == l_expected_sum) ? "ok" : "FAIL");
done:
    for (unsigned i = 0; i < a_nc; ++i) {
        if (l_srcs && l_srcs[i]) free(l_srcs[i]);
        if (l_socks[i][0] >= 0) close(l_socks[i][0]);
        if (l_socks[i][1] >= 0) close(l_socks[i][1]);
    }
    free(l_feeders); free(l_conns); free(l_exts);
    free(l_srcs); free(l_src_sz); free(l_sums);
    free(l_fts); free(l_wts); free(l_socks);
    free(l_sinks); free(l_sts);
    dap_io_destroy(l_io);
}

/* ================================================================== */
/*  Broadcast benchmark                                                */
/*                                                                     */
/*  1 source feeder -> worker[0] reads, pushes to WFQ                  */
/*  Processor broadcasts source OLB data to ALL conns' send_olbs       */
/*  Workers flush send_olbs normally -> sinks verify delivery           */
/* ================================================================== */

typedef struct {
    dap_conn_handle_t   *targets;      /*!< generation-checked broadcast destinations */
    unsigned             n_conns;
    bool                *force_complete;
    uint64_t             bcast_done;   /*!< bit i set = target[i] already received batch */
    size_t               frame_count;
    uint64_t             frame_bytes;
    size_t               send_count;
    size_t               send_defer;
    uint64_t             send_bytes;
} bcast_bench_t;

/**
 * @brief Fan-out callback: broadcast one source batch to every target
 *        handle, tolerating transient DAP_SEND_OVERFLOW via WFQ defer.
 *        DAP_SEND_TOO_LARGE is treated like CLOSED for bookkeeping (stop retry).
 *
 * No dap_sink_* abstraction is needed — a plain loop over the handle
 * array is enough.  Each dap_io_tx_send() does its own generation check,
 * watermark arming, and worker kick; the caller just decides whether to
 * defer (normal) or force-complete (shutdown) based on the bcast_done
 * bitmap, which tracks which targets have already been fed so retries
 * don't double-write.
 */
static dap_msg_rc_t s_bcast_fanout_rc(dap_conn_t *a_c, const char *a_batch, uint32_t a_bytes,
                                       void *a_arg)
{
    bcast_bench_t *l_s = a_arg;
    (void)a_c;
    size_t l_ok = 0;
    unsigned l_n = l_s->n_conns < 64 ? l_s->n_conns : 64;
    for (unsigned i = 0; i < l_n; ++i) {
        if (l_s->bcast_done & ((uint64_t)1 << i))
            continue;
        dap_send_rc_t l_rc = dap_io_tx_send(l_s->targets[i], a_batch, a_bytes);
        if (l_rc == DAP_SEND_OK) {
            l_s->bcast_done |= (uint64_t)1 << i;
            ++l_ok;
        } else if (l_rc == DAP_SEND_CLOSED || l_rc == DAP_SEND_TOO_LARGE) {
            /* Stale handle or permanent oversize — stop retrying this slot. */
            l_s->bcast_done |= (uint64_t)1 << i;
        }
        /* DAP_SEND_OVERFLOW: leave bit clear so next retry tries again. */
    }
    l_s->send_bytes += (uint64_t)l_ok * a_bytes;
    l_s->send_count += l_ok;

    uint64_t l_full = (l_n >= 64) ? ~(uint64_t)0 : (((uint64_t)1 << l_n) - 1);
    bool l_force = l_s->force_complete && *l_s->force_complete;
    if (!l_force && l_s->bcast_done != l_full) {
        ++l_s->send_defer;
        return DAP_MSG_DEFER;
    }
    l_s->bcast_done = 0;

    const char *l_p = a_batch, *l_pe = l_p + a_bytes;
    while (l_p + FRAME_HDR_SIZE <= l_pe) {
        size_t l_fsz = s_frame_size(((test_frame_hdr_t *)l_p)->size);
        if (l_p + l_fsz > l_pe) break;
        l_p += l_fsz;
        ++l_s->frame_count;
    }
    l_s->frame_bytes += a_bytes;
    return DAP_MSG_DONE;
}

static dap_msg_rc_t s_bcast_batch_cb(const dap_batch_task_t *a_task, void *a_arg)
{
    bcast_bench_t *l_s = a_arg;
    bool l_force = l_s->force_complete && *l_s->force_complete;
    return dap_proc_exec_batch_rc(a_task, l_force, s_bcast_fanout_rc, l_s);
}

static void s_bench_broadcast(unsigned a_nconn, size_t a_nframes,
                               size_t a_min, size_t a_max)
{
    dap_io_t *l_io = dap_io_create(a_nconn, 1);
    if (!l_io) { printf("  SKIP (io)\n"); return; }

    dap_conn_t        **l_conns   = calloc(a_nconn, sizeof(*l_conns));
    dap_conn_handle_t  *l_targets = calloc(a_nconn, sizeof(*l_targets));
    bench_ext_t        *l_exts    = calloc(a_nconn, sizeof(*l_exts));
    int               (*l_sk)[2] = malloc(a_nconn * sizeof(*l_sk));
    pthread_t          *l_wts    = calloc(a_nconn, sizeof(pthread_t));
    sink_ctx_t         *l_sinks  = calloc(a_nconn, sizeof(*l_sinks));
    pthread_t          *l_sts    = calloc(a_nconn, sizeof(pthread_t));

    size_t l_src_sz = 0; uint64_t l_src_sum = 0;
    uint8_t *l_src = s_generate_frames(a_nframes, a_min, a_max,
                                        42u, &l_src_sz, &l_src_sum);
    if (!l_src) { printf("  SKIP (gen)\n"); goto done; }

    for (unsigned i = 0; i < a_nconn; i++)
        l_sk[i][0] = l_sk[i][1] = -1;

    for (unsigned i = 0; i < a_nconn; i++) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, l_sk[i]) < 0) goto done;
        fcntl(l_sk[i][0], F_SETFL, fcntl(l_sk[i][0], F_GETFL) | O_NONBLOCK);
        fcntl(l_sk[i][1], F_SETFL, fcntl(l_sk[i][1], F_GETFL) | O_NONBLOCK);

        size_t l_mf = s_frame_size((uint32_t)a_max);
        l_exts[i] = (bench_ext_t){
            .parser = { .max_frame_sz = (uint32_t)l_mf }, .max_frame = l_mf };
        (void)dap_io_olb_ext_setup(&l_exts[i].olb, &l_exts[i].olb_parser,
            s_frame_parse, s_frame_compact);
        l_conns[i] = dap_io_conn_open(l_io, i, DAP_IO_SOCK, l_sk[i][0],
                                       OLB_CAPACITY, NULL, dap_io_rx_bridge, &l_exts[i], l_mf);
        if (!l_conns[i]) goto done;
    }

    feeder_ctx_t l_feeder = {
        .fd = l_sk[0][1], .src = l_src, .src_size = l_src_sz };
    for (unsigned i = 0; i < a_nconn; i++)
        l_sinks[i] = (sink_ctx_t){ .fd = l_sk[i][1] };

    for (unsigned i = 0; i < a_nconn; i++) {
        /* Handle captured on the main setup thread, immediately after
         * conn_open: the slot cannot possibly be reclaimed yet, so
         * handle_from_live is safe. */
        l_targets[i] = dap_conn_handle_from_live(l_conns[i]);
    }
    bcast_bench_t l_bb = {
        .targets = l_targets, .n_conns = a_nconn };
    dap_proc_ctx_t *l_bctx = dap_io_proc(l_io, 0);
    (void)dap_io_proc_set_batch_cb(l_io, 0, s_bcast_batch_cb, &l_bb);
    l_bb.force_complete = &l_bctx->force_complete;

    pthread_t l_pth, l_fth;
    uint64_t l_t0 = dap_nanotime_now();

    for (unsigned i = 0; i < a_nconn; i++)
        pthread_create(&l_sts[i], NULL, s_sink_thread, &l_sinks[i]);
    pthread_create(&l_pth, NULL, s_proc_thread, l_bctx);
    for (unsigned i = 0; i < a_nconn; i++)
        pthread_create(&l_wts[i], NULL, s_worker_thread, dap_io_worker(l_io, i));
    pthread_create(&l_fth, NULL, s_feeder, &l_feeder);

    pthread_join(l_fth, NULL);

    while (l_bb.frame_count < a_nframes)
        sched_yield();

    dap_io_shutdown(l_io);
    pthread_join(l_pth, NULL);

    for (unsigned i = 0; i < a_nconn; i++) {
        dap_worker_request_stop(dap_io_worker(l_io, i));
    }
    for (unsigned i = 0; i < a_nconn; i++)
        pthread_join(l_wts[i], NULL);

    size_t l_final_flush = 0;
    for (bool l_more = true; l_more; ) {
        l_more = false;
        for (unsigned i = 0; i < a_nconn; i++) {
            if (!l_conns[i] || !l_conns[i]->send_olb || l_sk[i][0] < 0)
                continue;
            ssize_t l_f = dap_send_olb_flush(l_conns[i]->send_olb, l_sk[i][0]);
            if (l_f > 0) { l_final_flush += (size_t)l_f; l_more = true; }
            else if (l_f < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                l_more = true;
        }
    }
    for (unsigned i = 0; i < a_nconn; i++) {
        close(l_sk[i][0]);
        l_sk[i][0] = -1;
    }
    for (unsigned i = 0; i < a_nconn; i++)
        pthread_join(l_sts[i], NULL);

    uint64_t l_elapsed = dap_nanotime_now() - l_t0;

    bool l_ok = true;
    for (unsigned i = 0; i < a_nconn; i++) {
        uint64_t l_got = atomic_load_explicit(&l_sinks[i].recv_bytes,
                                               memory_order_acquire);
        if (l_got != l_src_sz) {
            printf("  conn %u: expected %zu, got %llu\n",
                   i, l_src_sz, (unsigned long long)l_got);
            l_ok = false;
        }
    }

    double l_sec = (double)l_elapsed / 1e9;
    printf("  1Fx%uWx1P: bcast %zu frames  in:%5.2f  fan-out:%5.2f Gbps  "
           "defer %zu",
           a_nconn, l_bb.frame_count,
           (double)l_src_sz * 8.0 / l_sec / 1e9,
           (double)l_bb.send_bytes * 8.0 / l_sec / 1e9,
           l_bb.send_defer);
#ifdef DAP_IO_STATS
    { dap_proc_stats_t *l_ps = &l_io->proc_stats[0];
      if (l_ps->batches_deferred) printf(" prc_def %zu", l_ps->batches_deferred);
      printf(" busy %.0f%%", l_elapsed ? (double)l_ps->busy_ns / (double)l_elapsed * 100.0 : 0.0);
      if (l_ps->defer_oom) printf(" oom %zu", l_ps->defer_oom);
    }
#endif
    printf("  %s\n", l_ok ? "OK" : "MISMATCH");

done:
    for (unsigned i = 0; i < a_nconn; i++) {
        if (l_sk[i][0] >= 0) close(l_sk[i][0]);
        if (l_sk[i][1] >= 0) close(l_sk[i][1]);
    }
    free(l_conns); free(l_exts); free(l_targets);
    free(l_sk); free(l_wts); free(l_sinks); free(l_sts);
    if (l_src) free(l_src);
    dap_io_destroy(l_io);
}

/* ================================================================== */
/*  Stream benchmark: raw bytes, fixed-chunk slicing on OLB            */
/* ================================================================== */

static inline uint8_t *s_generate_raw_bytes(size_t a_total, uint32_t a_seed,
                                             uint64_t *a_sum)
{
    uint8_t *l_buf = malloc(a_total);
    if (!l_buf) return NULL;
    uint32_t l_st = a_seed;
    for (size_t i = 0; i + 7 < a_total; i += 8) {
        uint64_t l_v = ((uint64_t)s_xorshift32(&l_st) << 32) | s_xorshift32(&l_st);
        memcpy(l_buf + i, &l_v, 8);
    }
    *a_sum = s_checksum_u64(l_buf, (uint32_t)a_total);
    return l_buf;
}

static void s_stream_batch_cb(dap_conn_t *a_c, const char *a_batch, uint32_t a_bytes,
                               void *a_arg)
{
    (void)a_c;
    proc_bench_t *l_b = a_arg;
    l_b->send_bytes += a_bytes;
    ++l_b->send_count;
    l_b->frame_checksum += s_checksum_u64(a_batch, a_bytes);
    l_b->frame_bytes += a_bytes;
    ++l_b->frame_count;
}

static dap_msg_rc_t s_bench_echo_stream_rc(dap_conn_t *a_c, const char *a_batch, uint32_t a_bytes,
                                            void *a_arg)
{
    dap_send_olb_result_t l_wr = dap_send_olb_write(a_c->send_olb, a_batch, a_bytes);
    if (l_wr == DAP_SEND_OLB_FULL) {
        dap_worker_conn_notify_send(a_c);
        return DAP_MSG_DEFER;
    }
    if (l_wr != DAP_SEND_OLB_OK)
        return DAP_MSG_DROP;
    s_stream_batch_cb(a_c, a_batch, a_bytes, a_arg);
    dap_worker_conn_notify_send(a_c);
    return DAP_MSG_DONE;
}

static dap_msg_rc_t s_bench_echo_stream_batch_cb(const dap_batch_task_t *a_task, void *a_arg)
{
    return dap_proc_exec_batch_rc(a_task, dap_tls_proc && dap_tls_proc->force_complete,
                                  s_bench_echo_stream_rc, a_arg);
}

static void s_bench_stream(unsigned a_nw, unsigned a_nc, size_t a_total_per_conn,
                            uint32_t a_chunk_size)
{
    dap_io_t *l_io = dap_io_create(a_nw, 1);
    if (!l_io) { printf("  %uFx%uWx1P SKIP\n", a_nc, a_nw); return; }

    size_t l_total = (a_total_per_conn / a_chunk_size) * a_chunk_size;
    size_t l_nchunks = l_total / a_chunk_size;

    feeder_ctx_t       *l_feeders = calloc(a_nc, sizeof(*l_feeders));
    dap_conn_t        **l_conns   = calloc(a_nc, sizeof(*l_conns));
    stream_ext_t       *l_exts    = calloc(a_nc, sizeof(*l_exts));
    uint8_t           **l_srcs    = calloc(a_nc, sizeof(*l_srcs));
    uint64_t           *l_sums    = calloc(a_nc, sizeof(*l_sums));
    pthread_t          *l_fts     = calloc(a_nc, sizeof(pthread_t));
    pthread_t          *l_wts     = calloc(a_nw, sizeof(pthread_t));
    int (*l_socks)[2]             = malloc(a_nc * sizeof(*l_socks));
    sink_ctx_t         *l_sinks   = calloc(a_nc, sizeof(*l_sinks));
    pthread_t          *l_sts     = calloc(a_nc, sizeof(pthread_t));

    proc_bench_t l_bench = {0};

    for (unsigned i = 0; i < a_nc; ++i)
        l_socks[i][0] = l_socks[i][1] = -1;

    for (unsigned i = 0; i < a_nc; ++i) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, l_socks[i]) < 0)
            { printf("  %uFx%uWx1P SKIP (sock)\n", a_nc, a_nw); goto done; }
        fcntl(l_socks[i][0], F_SETFL, fcntl(l_socks[i][0], F_GETFL) | O_NONBLOCK);
        fcntl(l_socks[i][1], F_SETFL, fcntl(l_socks[i][1], F_GETFL) | O_NONBLOCK);

        l_srcs[i] = s_generate_raw_bytes(l_total, 42u + i, &l_sums[i]);
        if (!l_srcs[i]) { printf("  %uFx%uWx1P SKIP (gen)\n", a_nc, a_nw); goto done; }

        unsigned l_wid = i % a_nw;
        l_exts[i] = (stream_ext_t){ .chunk_size = a_chunk_size };
        (void)dap_io_olb_ext_setup(&l_exts[i].olb, &l_exts[i].olb_parser,
            s_stream_parse, NULL);
        l_conns[i] = dap_io_conn_open(l_io, l_wid, DAP_IO_SOCK, l_socks[i][0],
                                       OLB_CAPACITY, NULL, dap_io_rx_bridge, &l_exts[i], 0);
        if (!l_conns[i]) { printf("  %uFx%uWx1P SKIP (conn)\n", a_nc, a_nw); goto done; }

        l_feeders[i] = (feeder_ctx_t){
            .fd = l_socks[i][1], .src = l_srcs[i], .src_size = l_total };
    }

    uint64_t l_expected_sum = 0;
    for (unsigned i = 0; i < a_nc; ++i)
        l_expected_sum += l_sums[i];

    dap_proc_ctx_t *l_pctx = dap_io_proc(l_io, 0);
    (void)dap_io_proc_set_batch_cb(l_io, 0, s_bench_echo_stream_batch_cb, &l_bench);

    size_t l_total_chunks = a_nc * l_nchunks;
    pthread_t l_pth;
    uint64_t l_t0 = dap_nanotime_now();

    for (unsigned i = 0; i < a_nc; ++i) {
        l_sinks[i] = (sink_ctx_t){ .fd = l_socks[i][1] };
        pthread_create(&l_sts[i], NULL, s_sink_thread, &l_sinks[i]);
    }
    pthread_create(&l_pth, NULL, s_proc_thread, l_pctx);
    for (unsigned i = 0; i < a_nw; ++i)
        pthread_create(&l_wts[i], NULL, s_worker_thread, dap_io_worker(l_io, i));
    for (unsigned i = 0; i < a_nc; ++i)
        pthread_create(&l_fts[i], NULL, s_feeder, &l_feeders[i]);

    for (unsigned i = 0; i < a_nc; ++i) pthread_join(l_fts[i], NULL);
    {
        uint64_t l_expected_bytes = (uint64_t)a_nc * l_total;
        while (atomic_load_explicit(&l_bench.frame_bytes, memory_order_acquire) < l_expected_bytes)
            sched_yield();
    }

    dap_io_shutdown(l_io);
    pthread_join(l_pth, NULL);

    for (unsigned i = 0; i < a_nw; ++i)
        dap_worker_request_stop(dap_io_worker(l_io, i));
    for (unsigned i = 0; i < a_nw; ++i) pthread_join(l_wts[i], NULL);

    size_t l_final_flush = 0;
    for (bool l_more = true; l_more; ) {
        l_more = false;
        for (unsigned i = 0; i < a_nc; ++i) {
            if (!l_conns[i] || !l_conns[i]->send_olb || l_socks[i][0] < 0)
                continue;
            ssize_t l_f = dap_send_olb_flush(l_conns[i]->send_olb, l_socks[i][0]);
            if (l_f > 0) { l_final_flush += (size_t)l_f; l_more = true; }
            else if (l_f < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                l_more = true;
        }
    }
    for (unsigned i = 0; i < a_nc; ++i) {
        close(l_socks[i][0]);
        l_socks[i][0] = -1;
    }
    for (unsigned i = 0; i < a_nc; ++i)
        pthread_join(l_sts[i], NULL);

    uint64_t l_elapsed = dap_nanotime_now() - l_t0;
    uint64_t l_total_bytes = a_nc * l_total;

    size_t l_total_sus = 0, l_total_cmp = 0, l_total_mmv = 0, l_total_early = 0, l_total_send = 0;
#ifdef DAP_IO_STATS
    for (unsigned i = 0; i < a_nw; ++i) {
        l_total_sus  += l_io->worker_stats[i].suspends;
        l_total_cmp  += l_io->worker_stats[i].compacts;
        l_total_send += l_io->worker_stats[i].send_bytes;
    }
#endif
    for (unsigned i = 0; i < a_nc; ++i) {
        if (l_conns[i] && l_conns[i]->olb) {
            l_total_mmv   += l_conns[i]->olb->memmove_compacts;
            l_total_early += l_conns[i]->olb->early_compacts;
        }
    }

    double l_sec = (double)l_elapsed / 1e9;
    size_t l_chunk_count = atomic_load_explicit(&l_bench.frame_count, memory_order_relaxed);
    size_t l_send_count = atomic_load_explicit(&l_bench.send_count, memory_order_relaxed);
    uint64_t l_frame_checksum = atomic_load_explicit(&l_bench.frame_checksum, memory_order_relaxed);
    bool l_chk_ok = (l_frame_checksum == l_expected_sum);
    uint64_t l_sink_bytes = 0;
    for (unsigned i = 0; i < a_nc; ++i)
        l_sink_bytes += atomic_load_explicit(&l_sinks[i].recv_bytes, memory_order_acquire);
    uint64_t l_flushed_total = l_total_send + l_final_flush;
    bool l_sink_ok = (l_sink_bytes == l_flushed_total);

    printf("  %uFx%uWx1P: %6.2f Gbps  chunks:%zu/%zu  echo:%zu"
           "  sus %zu cmp %zu(early %zu mmv %zu)",
           a_nc, a_nw,
           (double)l_total_bytes * 8.0 / l_sec / 1e9,
           l_chunk_count, l_total_chunks,
           l_send_count,
           l_total_sus, l_total_cmp, l_total_early, l_total_mmv);
#ifdef DAP_IO_STATS
    { dap_proc_stats_t *l_ps = &l_io->proc_stats[0];
      printf(" busy %.0f%%", l_elapsed ? (double)l_ps->busy_ns / (double)l_elapsed * 100.0 : 0.0);
    }
#endif
    printf("  %s\n", (l_chk_ok && l_sink_ok) ? "OK" : "MISMATCH");
    if (!l_chk_ok)
        printf("    !! checksum: got %llu expected %llu\n",
               (unsigned long long)l_frame_checksum, (unsigned long long)l_expected_sum);
    if (!l_sink_ok)
        printf("    !! sink: got %llu  flushed %llu  w_flush %zu  fin_flush %zu  (delta %lld)\n",
               (unsigned long long)l_sink_bytes, (unsigned long long)l_flushed_total,
               l_total_send, l_final_flush,
               (long long)((int64_t)l_sink_bytes - (int64_t)l_flushed_total));

done:
    for (unsigned i = 0; i < a_nc; ++i) {
        if (l_srcs && l_srcs[i]) free(l_srcs[i]);
        if (l_socks[i][0] >= 0) close(l_socks[i][0]);
        if (l_socks[i][1] >= 0) close(l_socks[i][1]);
    }
    free(l_feeders); free(l_conns); free(l_exts);
    free(l_srcs); free(l_sums);
    free(l_fts); free(l_wts);
    free(l_socks); free(l_sinks); free(l_sts);
    dap_io_destroy(l_io);
}

/* ================================================================== */
/*  Pure SPSC lane benchmark (no sockets, no OLB)                      */
/*                                                                     */
/*  N producer threads push DAP_MSG_BATCH directly into WFQ SPSC       */
/*  lanes.  Processor drains via batch_cb that just counts.            */
/*  Measures raw SPSC lane push+drain throughput.                      */
/* ================================================================== */

typedef struct {
    dap_vmqueue_mpsc_t *wfq;
    unsigned            lane;
    unsigned            n_tasks;
    _Atomic(bool)      *shutdown;
    _Atomic(uint64_t)   done_count;
} spsc_sender_ctx_t;

static void *s_spsc_sender(void *a_arg)
{
    spsc_sender_ctx_t *l_ctx = a_arg;
    uint64_t l_count = 0;
    for (unsigned i = 0; i < l_ctx->n_tasks; ++i) {
        if (atomic_load_explicit(l_ctx->shutdown, memory_order_relaxed))
            break;
        /* Synthetic SPSC throughput test: no real connection, the
         * handle's .gen slot is repurposed to carry a per-lane tag
         * for checksum verification in the drain callback. */
        dap_batch_task_t l_t = {
            .conn      = { .c = NULL, .gen = l_ctx->lane + 1 },
            .batch_end = (uint32_t)(i + 1) };
        while (!dap_vmqueue_mpsc_push(l_ctx->wfq, l_ctx->lane,
                                       DAP_MSG_BATCH, DAP_WFQ_PRI_NORM,
                                       &l_t, sizeof(l_t))) {
            if (atomic_load_explicit(l_ctx->shutdown, memory_order_relaxed))
                goto out;
            sched_yield();
        }
        dap_bus_proc_wake(l_ctx->wfq);
        ++l_count;
    }
out:
    atomic_store_explicit(&l_ctx->done_count, l_count, memory_order_release);
    return NULL;
}

static _Atomic(size_t) s_spsc_proc_count;
static _Atomic(uint64_t) s_spsc_checksum;

static dap_msg_rc_t s_spsc_batch_cb(const dap_batch_task_t *a_t, void *a_arg)
{
    (void)a_arg;
    atomic_fetch_add_explicit(&s_spsc_proc_count, 1, memory_order_relaxed);
    atomic_fetch_xor_explicit(&s_spsc_checksum,
        a_t->conn.gen ^ ((uint64_t)a_t->batch_end << 16),
        memory_order_relaxed);
    return DAP_MSG_DONE;
}

static void s_bench_spsc(unsigned a_n_producers, unsigned a_tasks_per_producer)
{
    dap_io_t *l_io = dap_io_create(a_n_producers, 1);
    if (!l_io) { printf("  SKIP (io)\n"); return; }

    atomic_store_explicit(&s_spsc_proc_count, 0, memory_order_relaxed);
    atomic_store_explicit(&s_spsc_checksum, 0, memory_order_relaxed);

    dap_proc_ctx_t *l_pctx = dap_io_proc(l_io, 0);
    (void)dap_io_proc_set_batch_cb(l_io, 0, s_spsc_batch_cb, NULL);

    spsc_sender_ctx_t *l_ctx = calloc(a_n_producers, sizeof(*l_ctx));
    pthread_t *l_tids = calloc(a_n_producers, sizeof(pthread_t));

    for (unsigned i = 0; i < a_n_producers; ++i) {
        unsigned l_lane = DAP_WFQ_PRI_LANE(DAP_WFQ_PRI_NORM, i, a_n_producers);
        l_ctx[i] = (spsc_sender_ctx_t){
            .wfq = l_pctx->wfq,
            .lane = l_lane,
            .n_tasks = a_tasks_per_producer,
            .shutdown = &l_io->shutdown };
    }

    uint64_t l_total_expected = (uint64_t)a_n_producers * a_tasks_per_producer;
    pthread_t l_pth;
    uint64_t l_t0 = dap_nanotime_now();
    pthread_create(&l_pth, NULL, s_proc_thread, l_pctx);
    for (unsigned i = 0; i < a_n_producers; ++i)
        pthread_create(&l_tids[i], NULL, s_spsc_sender, &l_ctx[i]);

    for (unsigned i = 0; i < a_n_producers; ++i)
        pthread_join(l_tids[i], NULL);

    uint64_t l_total_pushed = 0;
    for (unsigned i = 0; i < a_n_producers; ++i)
        l_total_pushed += atomic_load_explicit(&l_ctx[i].done_count, memory_order_acquire);

    while (atomic_load_explicit(&s_spsc_proc_count, memory_order_acquire) < l_total_pushed)
        sched_yield();

    dap_io_shutdown(l_io);
    pthread_join(l_pth, NULL);

    uint64_t l_elapsed = dap_nanotime_now() - l_t0;
    double l_sec = (double)l_elapsed / 1e9;
    size_t l_proc = atomic_load_explicit(&s_spsc_proc_count, memory_order_acquire);
    bool l_ok = (l_proc == l_total_pushed);

    printf("  %uPx1C: %6.2f Mops  pushed %llu  proc %zu  %.1f ms",
           a_n_producers,
           (double)l_total_pushed / l_sec / 1e6,
           (unsigned long long)l_total_pushed,
           l_proc,
           l_sec * 1e3);
#ifdef DAP_IO_STATS
    { dap_proc_stats_t *l_ps = &l_io->proc_stats[0];
      printf("  busy %.0f%%", l_elapsed ? (double)l_ps->busy_ns / (double)l_elapsed * 100.0 : 0.0);
    }
#endif
    printf("  %s\n", l_ok ? "OK" : "MISMATCH");

    free(l_ctx); free(l_tids);
    dap_io_destroy(l_io);
}

/* ================================================================== */
/*  Ext-stack benchmark (Treiber MPSC heap messages only)              */
/*                                                                     */
/*  Pure Treiber stack throughput: N sender threads push heap messages  */
/*  to the processor's ext-stack.  No sockets, no OLB, no workers.     */
/*  Processor drains ext-stack via heap_cb in its main loop.           */
/* ================================================================== */

#define N_EXT_THREADS 8
#define EXT_TASKS_PER_THREAD 2000

static void s_bench_ext(unsigned a_n_threads, unsigned a_tasks_per_thread)
{
    dap_io_t *l_io = dap_io_create(1, 1);
    if (!l_io) { printf("  SKIP (io)\n"); return; }

    proc_bench_t l_bench = {0};
    dap_proc_ctx_t *l_pctx = dap_io_proc(l_io, 0);
    (void)dap_io_proc_set_heap_cb(l_io, 0, s_proc_heap_cb, &l_bench);

    ext_sender_ctx_t *l_ctx = calloc(a_n_threads, sizeof(*l_ctx));
    pthread_t *l_tids = calloc(a_n_threads, sizeof(pthread_t));

    for (unsigned i = 0; i < a_n_threads; ++i) {
        l_ctx[i] = (ext_sender_ctx_t){
            .proc = l_pctx,
            .shutdown = &l_io->shutdown,
            .n_tasks = a_tasks_per_thread };
    }

    pthread_t l_pth;
    uint64_t l_t0 = dap_nanotime_now();
    pthread_create(&l_pth, NULL, s_proc_thread, l_pctx);
    for (unsigned i = 0; i < a_n_threads; ++i)
        pthread_create(&l_tids[i], NULL, s_ext_heap_sender, &l_ctx[i]);

    for (unsigned i = 0; i < a_n_threads; ++i)
        pthread_join(l_tids[i], NULL);

    uint64_t l_total_pushed = 0;
    for (unsigned i = 0; i < a_n_threads; ++i)
        l_total_pushed += atomic_load_explicit(&l_ctx[i].done_count, memory_order_acquire);

    while (atomic_load_explicit(&l_bench.ext_count, memory_order_acquire) < l_total_pushed)
        sched_yield();

    dap_io_shutdown(l_io);
    pthread_join(l_pth, NULL);

    uint64_t l_elapsed = dap_nanotime_now() - l_t0;
    double l_sec = (double)l_elapsed / 1e9;
    size_t l_ext_count = atomic_load_explicit(&l_bench.ext_count, memory_order_relaxed);
    bool l_ok = (l_ext_count == l_total_pushed);

    printf("  %uTx1P: %6.2f Mops  pushed %llu  proc %zu  %.1f ms  %s\n",
           a_n_threads,
           (double)l_total_pushed / l_sec / 1e6,
           (unsigned long long)l_total_pushed,
           l_ext_count,
           l_sec * 1e3,
           l_ok ? "OK" : "MISMATCH");

    free(l_ctx); free(l_tids);
    dap_io_destroy(l_io);
}

/* ================================================================== */
/*  Main                                                               */
/* ================================================================== */

#define REPEATS 2

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("===========================================================================\n");
    printf("  Pipeline: Feeders (socket) -> Workers (epoll+OLB+WFQ) -> Processor\n");
    printf("  OLB: %zu KB   WFQ FAST:%dK  NORM:%dK  BG:%dK  OVF:flex\n",
           OLB_CAPACITY / 1024,
           DAP_WFQ_CAP_FAST / 1024, DAP_WFQ_CAP_NORM / 1024, DAP_WFQ_CAP_BG / 1024);
    printf("  Frame task: %zu B (OLB zero-copy)   Timer task: %zu B\n",
           sizeof(dap_vmqueue_hdr_t) + sizeof(dap_batch_task_t),
           sizeof(dap_vmqueue_hdr_t) + sizeof(dap_callback_task_t));
    printf("  Timers: W[%d+%d us] P[%d us]   Quotas: F=%d N=%d B=%d\n",
           TIMER_50MS_US, TIMER_200MS_US, TIMER_PROC_US,
           DAP_WFQ_FAST_QUOTA, DAP_WFQ_NORM_QUOTA, DAP_WFQ_BG_QUOTA);
    printf("  Frames -> NORM (spin)   Timers -> BG (backpressure)\n");
    printf("===========================================================================\n\n");

 /* disabled for gdb debugging of mode-switch test */
    /* ================================================================== */
    /*  Section 1: Dedicated workers (1 conn per worker, SPSC isolation)   */
    /*  Each worker owns a private SPSC lane set to the processor.         */
    /* ================================================================== */

    /*  1a — Frame pipeline (SPSC lanes only)                               */
    /*  socket -> OLB -> worker parse -> WFQ SPSC NORM -> proc batch_cb   */
    /*  (test echo) -> send_olb -> worker flush -> sink                     */
    struct { unsigned nw; unsigned nc; size_t nf; size_t min; size_t max; } l_ded[] = {
        { 1, 1, 2000000, 256, 256  },
        { 2, 2, 1000000, 256, 256  },
        { 4, 4,  500000, 256, 256  },
        { 1, 1, 1000000, 64,  4096 },
        { 2, 2,  500000, 64,  4096 },
        { 4, 4,  250000, 64,  4096 },
    };
    for (size_t c = 0; c < sizeof(l_ded)/sizeof(l_ded[0]); ++c) {
        printf("--- Dedicated %uFx%uWx1P, %zu frames/conn, payload %zu-%zu B ---\n",
               l_ded[c].nc, l_ded[c].nw, l_ded[c].nf, l_ded[c].min, l_ded[c].max);
        for (int r = 0; r < REPEATS; ++r)
            s_bench(l_ded[c].nw, l_ded[c].nc, l_ded[c].nf,
                    l_ded[c].min, l_ded[c].max);
        printf("\n");
    }

    /*  1b — Stream pipeline (raw byte throughput, no frame parsing)       */
    /*  socket -> OLB -> worker chunk slicing -> WFQ -> proc               */
    struct { unsigned nw; unsigned nc; size_t total; uint32_t chunk; } l_ded_s[] = {
        { 1, 1, Mbytes(512), 256  },
        { 2, 2, Mbytes(256), 256  },
        { 4, 4, Mbytes(128), 256  },
        { 1, 1, Mbytes(512), 4096 },
        { 2, 2, Mbytes(256), 4096 },
        { 4, 4, Mbytes(128), 4096 },
    };
    for (size_t c = 0; c < sizeof(l_ded_s)/sizeof(l_ded_s[0]); ++c) {
        printf("--- Dedicated %uFx%uWx1P stream, %zuMB/conn, chunk %u B ---\n",
               l_ded_s[c].nc, l_ded_s[c].nw,
               l_ded_s[c].total >> 20, l_ded_s[c].chunk);
        for (int r = 0; r < REPEATS; ++r)
            s_bench_stream(l_ded_s[c].nw, l_ded_s[c].nc,
                           l_ded_s[c].total, l_ded_s[c].chunk);
        printf("\n");
    }

    /*  1c — Frame pipeline, SYNC mode (worker-inline, no processor)       */
    for (size_t c = 0; c < sizeof(l_ded)/sizeof(l_ded[0]); ++c) {
        printf("--- Dedicated %uFx%uW SYNC, %zu frames/conn, payload %zu-%zu B ---\n",
               l_ded[c].nc, l_ded[c].nw, l_ded[c].nf, l_ded[c].min, l_ded[c].max);
        for (int r = 0; r < REPEATS; ++r)
            s_bench_sync(l_ded[c].nw, l_ded[c].nc, l_ded[c].nf,
                         l_ded[c].min, l_ded[c].max);
        printf("\n");
    }

    /*  1d — Stream pipeline, SYNC mode                                    */
    for (size_t c = 0; c < sizeof(l_ded_s)/sizeof(l_ded_s[0]); ++c) {
        printf("--- Dedicated %uFx%uW SYNC stream, %zuMB/conn, chunk %u B ---\n",
               l_ded_s[c].nc, l_ded_s[c].nw,
               l_ded_s[c].total >> 20, l_ded_s[c].chunk);
        for (int r = 0; r < REPEATS; ++r)
            s_bench_stream_sync(l_ded_s[c].nw, l_ded_s[c].nc,
                                l_ded_s[c].total, l_ded_s[c].chunk);
        printf("\n");
    }

    /* ================================================================== */
    /*  Section 2: Shared worker (N conns on 1 worker)                     */
    /*  Tests OLB multiplexing, pending_bits scan, rescan_mask kick.       */
    /*  All conns share 1 SPSC lane set — no lane isolation.              */
    /* ================================================================== */

    /*  2a — Frame pipeline                                                */
    struct { unsigned nc; size_t nf; size_t min; size_t max; } l_shr[] = {
        { 2, 1000000, 256, 256  },
        { 4,  500000, 256, 256  },
        { 2,  500000, 64,  4096 },
        { 4,  250000, 64,  4096 },
    };
    for (size_t c = 0; c < sizeof(l_shr)/sizeof(l_shr[0]); ++c) {
        printf("--- Shared %uFx1Wx1P, %zu frames/conn, payload %zu-%zu B ---\n",
               l_shr[c].nc, l_shr[c].nf, l_shr[c].min, l_shr[c].max);
        for (int r = 0; r < REPEATS; ++r)
            s_bench(1, l_shr[c].nc, l_shr[c].nf, l_shr[c].min, l_shr[c].max);
        printf("\n");
    }

    /*  2b — Stream pipeline (shared worker)                               */
    struct { unsigned nc; size_t total; uint32_t chunk; } l_shr_s[] = {
        { 2, Mbytes(256), 256  },
        { 4, Mbytes(128), 256  },
        { 2, Mbytes(256), 4096 },
        { 4, Mbytes(128), 4096 },
    };
    for (size_t c = 0; c < sizeof(l_shr_s)/sizeof(l_shr_s[0]); ++c) {
        printf("--- Shared %uFx1Wx1P stream, %zuMB/conn, chunk %u B ---\n",
               l_shr_s[c].nc, l_shr_s[c].total >> 20, l_shr_s[c].chunk);
        for (int r = 0; r < REPEATS; ++r)
            s_bench_stream(1, l_shr_s[c].nc, l_shr_s[c].total, l_shr_s[c].chunk);
        printf("\n");
    }

    /*  2c — Frame pipeline, SYNC mode (shared worker)                     */
    for (size_t c = 0; c < sizeof(l_shr)/sizeof(l_shr[0]); ++c) {
        printf("--- Shared %uFx1W SYNC, %zu frames/conn, payload %zu-%zu B ---\n",
               l_shr[c].nc, l_shr[c].nf, l_shr[c].min, l_shr[c].max);
        for (int r = 0; r < REPEATS; ++r)
            s_bench_sync(1, l_shr[c].nc, l_shr[c].nf, l_shr[c].min, l_shr[c].max);
        printf("\n");
    }

    /*  2d — Stream pipeline, SYNC mode (shared worker)                    */
    for (size_t c = 0; c < sizeof(l_shr_s)/sizeof(l_shr_s[0]); ++c) {
        printf("--- Shared %uFx1W SYNC stream, %zuMB/conn, chunk %u B ---\n",
               l_shr_s[c].nc, l_shr_s[c].total >> 20, l_shr_s[c].chunk);
        for (int r = 0; r < REPEATS; ++r)
            s_bench_stream_sync(1, l_shr_s[c].nc, l_shr_s[c].total, l_shr_s[c].chunk);
        printf("\n");
    }

    /* ================================================================== */
    /*  Section 3: Pure queue throughput (no sockets, no OLB)              */
    /*  3a — SPSC lanes: N producers -> WFQ SPSC NORM -> proc batch_cb    */
    /*  3b — Treiber ext-stack: N threads -> heap post -> proc heap_cb    */
    /*  Both use 500K total messages for stable measurement.               */
    /* ================================================================== */

    struct { unsigned np; unsigned tpp; } l_spsc[] = {
        { 1,  500000 },
        { 2,  250000 },
        { 4,  125000 },
        { 8,  62500  },
    };
    for (size_t c = 0; c < sizeof(l_spsc)/sizeof(l_spsc[0]); ++c) {
        printf("--- SPSC lanes %uPx1C, %u msgs/producer ---\n",
               l_spsc[c].np, l_spsc[c].tpp);
        for (int r = 0; r < REPEATS; ++r)
            s_bench_spsc(l_spsc[c].np, l_spsc[c].tpp);
        printf("\n");
    }

    struct { unsigned nt; unsigned tpt; } l_ext[] = {
        { 1,  500000 },
        { 2,  250000 },
        { 4,  125000 },
        { 8,  62500  },
        { 16, 31250  },
    };
    for (size_t c = 0; c < sizeof(l_ext)/sizeof(l_ext[0]); ++c) {
        printf("--- Ext-stack %uTx1P, %u tasks/thread ---\n",
               l_ext[c].nt, l_ext[c].tpt);
        for (int r = 0; r < REPEATS; ++r)
            s_bench_ext(l_ext[c].nt, l_ext[c].tpt);
        printf("\n");
    }
 /* disabled sections 1-3 */

    /* ================================================================== */
    /*  Section 5: Mode switching (SYNC → ASYNC → SYNC)                   */
    /*  User's read_cb state machine switches mode by byte threshold.     */
    /*  Verifies: no frame loss, no checksum corruption across switches.  */
    /* ================================================================== */

    struct { unsigned nw; unsigned nc; size_t nf; size_t min; size_t max; } l_sw[] = {
        { 1, 1, 2000000, 256, 256  },
        { 2, 2, 1000000, 256, 256  },
        { 1, 1, 1000000, 64,  4096 },
        { 1, 2,  500000, 256, 256  },
    };
    for (size_t c = 0; c < sizeof(l_sw)/sizeof(l_sw[0]); ++c) {
        printf("--- Mode switch %uFx%uW, %zu frames/conn, payload %zu-%zu B ---\n",
               l_sw[c].nc, l_sw[c].nw, l_sw[c].nf, l_sw[c].min, l_sw[c].max);
        for (int r = 0; r < REPEATS; ++r)
            s_bench_mode_switch(l_sw[c].nw, l_sw[c].nc, l_sw[c].nf,
                                l_sw[c].min, l_sw[c].max);
        printf("\n");
    }

#if 0  /* disabled for gdb debugging of mode-switch test */
    /* ================================================================== */
    /*  Section 4: Broadcast (processor fan-out via batch_cb)              */
    /*  Data: socket -> worker[0] OLB -> WFQ -> proc batch_cb             */
    /*        -> N send_olbs -> worker[i] flush -> sink sockets           */
    /*  Tests: batch_cb path, defer queue (DEFER on send_olb full),       */
    /*         defer_q.mask serialisation, cross-worker send_olb write.   */
    /* ================================================================== */
    printf("--- Broadcast 1Fx2Wx1P, payload 64-512 B ---\n");
    s_bench_broadcast(2, 200000, 64, 512);
    printf("--- Broadcast 1Fx4Wx1P, payload 64-512 B ---\n");
    s_bench_broadcast(4, 100000, 64, 512);
    printf("--- Broadcast 1Fx2Wx1P, payload 128-4096 B ---\n");
    s_bench_broadcast(2, 50000, 128, 4096);
    printf("--- Broadcast 1Fx4Wx1P, payload 128-4096 B ---\n");
    s_bench_broadcast(4, 25000, 128, 4096);
    printf("\n");
#endif /* disabled section 4 */

    printf("===========================================================================\n");
    printf("  Done\n");
    printf("===========================================================================\n");
    return 0;
}
