/*
 * bench_wfq_throughput.c — WFQ throughput via generic typed API
 *
 * Pushes dap_batch_task_t (DAP_MSG_BATCH) through dap_wfq_post_batch,
 * drains via dap_proc_loop_run + batch_cb.  Consumer touches every
 * 64th byte of the referenced data pool.  Metric: GB/s.
 *
 * Uses ONLY the public API — no s_* internals.
 */
#define DAP_WFQ_FAST_QUOTA     512
#define DAP_WFQ_NORM_QUOTA     512
#define DAP_WFQ_BG_QUOTA       512

#include "test_helpers_enh.h"
#include "dap_io_advanced.h"

#include <signal.h>
#include <string.h>

#define FRAME_MIN  64
#define FRAME_MAX  4096
#define DATA_POOL_SIZE (256ULL * 1024 * 1024)

static uint8_t *g_data_pool;

/* ================================================================== */
/*  Schedule generation                                                */
/* ================================================================== */

typedef struct {
    uint32_t offset;
    uint32_t len;
} frame_desc_t;

typedef struct {
    frame_desc_t *descs;
    unsigned      count;
    uint64_t      total_bytes;
} frame_schedule_t;

static uint8_t s_touch_bytes(const uint8_t *a_ptr, uint32_t a_len)
{
    uint8_t l_acc = 0;
    for (uint32_t i = 0; i < a_len; i += 64)
        l_acc ^= a_ptr[i];
    if (a_len > 0) l_acc ^= a_ptr[a_len - 1];
    return l_acc;
}

static frame_schedule_t s_gen_schedule(unsigned a_wid, unsigned a_count)
{
    frame_schedule_t l_s = {};
    l_s.descs = malloc(a_count * sizeof(frame_desc_t));
    l_s.count = a_count;
    uint32_t l_rng = 0xDEAD0000 + a_wid;
    uint32_t l_off = (uint32_t)(a_wid * (DATA_POOL_SIZE / 32));
    for (unsigned i = 0; i < a_count; ++i) {
        l_rng ^= l_rng << 13; l_rng ^= l_rng >> 17; l_rng ^= l_rng << 5;
        uint32_t l_len = FRAME_MIN + (l_rng % (FRAME_MAX - FRAME_MIN + 1));
        l_s.descs[i].offset = l_off % (uint32_t)(DATA_POOL_SIZE - FRAME_MAX);
        l_s.descs[i].len = l_len;
        l_s.total_bytes += l_len;
        l_off += l_len;
    }
    return l_s;
}

static const char *s_status(uint64_t a_expect, uint64_t a_sent, uint64_t a_proc)
{
    if (a_sent != a_expect) return "INCOMPLETE";
    if (a_proc != a_sent)   return "MISMATCH";
    return "OK";
}

/* ================================================================== */
/*  Sender — uses dap_worker_push_batch (public API)                   */
/* ================================================================== */

typedef struct {
    dap_worker_t   *worker;
    _Atomic(bool)      *shutdown;
    frame_schedule_t   *sched;
    _Atomic(uint64_t)   done_bytes;
} sender_ctx_t;

static void *s_sender(void *a_arg)
{
    sender_ctx_t *l_ctx = a_arg;
    uint64_t l_bytes = 0;
    for (unsigned i = 0; i < l_ctx->sched->count; ++i) {
        if (atomic_load_explicit(l_ctx->shutdown, memory_order_relaxed))
            break;
        frame_desc_t *l_d = &l_ctx->sched->descs[i];
        dap_conn_handle_t l_h = { .c = NULL, .gen = l_d->offset };
        if (!dap_worker_push_batch(l_ctx->worker, l_h, l_d->len))
            break;
        l_bytes += l_d->len;
    }
    atomic_store_explicit(&l_ctx->done_bytes, l_bytes, memory_order_release);
    return NULL;
}

/* ================================================================== */
/*  Typed batch callback — user-level frame processing                 */
/* ================================================================== */

typedef struct {
    uint64_t proc_bytes;
    volatile uint8_t sink;
} bench_accum_t;

static dap_msg_rc_t s_batch_cb(const dap_batch_task_t *a_task, void *a_arg)
{
    bench_accum_t *l_a = a_arg;
    /* Synthetic bench: the sender encodes its pool offset into conn.gen
     * (no real connection is involved), and batch_end is the frame
     * length.  The batch_cb doesn't touch conn.c nor call resolve. */
    l_a->sink ^= s_touch_bytes(g_data_pool + (a_task->conn.gen % DATA_POOL_SIZE), a_task->batch_end);
    l_a->proc_bytes += a_task->batch_end;
    return DAP_MSG_DONE;
}

/* ================================================================== */
/*  Run helper — full dap_proc_loop_run (generic API)                  */
/* ================================================================== */

static void s_print_result(unsigned a_nw, unsigned a_tasks,
                           double a_sec, uint64_t a_expect, uint64_t a_sent,
                           uint64_t a_proc)
{
    printf("  %2u x %-6u  %.3fs  %6.2f GB/s  %s\n",
           a_nw, a_tasks, a_sec,
           (double)a_proc / a_sec / 1e9,
           s_status(a_expect, a_sent, a_proc));
}

static void s_run(unsigned a_nw, unsigned a_tasks,
                  frame_schedule_t *a_sch, uint64_t a_expect)
{
    dap_io_t *l_io = dap_io_create(a_nw, 1);
    if (!l_io) { printf("  SKIP\n"); return; }

    bench_accum_t l_acc = {};
    dap_proc_ctx_t *l_pctx = dap_io_proc(l_io, 0);
    (void)dap_io_proc_set_batch_cb(l_io, 0, s_batch_cb, &l_acc);

    sender_ctx_t *l_s = calloc(a_nw, sizeof(*l_s));
    for (unsigned i = 0; i < a_nw; ++i)
        l_s[i] = (sender_ctx_t){ .worker = dap_io_worker(l_io, i),
                   .shutdown = &l_io->shutdown,
                   .sched = &a_sch[i] };
    pthread_t *l_tids = calloc(a_nw, sizeof(pthread_t));
    pthread_t l_pth;
    uint64_t l_t0 = dap_nanotime_now();
    pthread_create(&l_pth, NULL, s_proc_thread, l_pctx);
    for (unsigned i = 0; i < a_nw; ++i)
        pthread_create(&l_tids[i], NULL, s_sender, &l_s[i]);
    for (unsigned i = 0; i < a_nw; ++i) pthread_join(l_tids[i], NULL);
    uint64_t l_sent = 0;
    for (unsigned i = 0; i < a_nw; ++i)
        l_sent += atomic_load_explicit(&l_s[i].done_bytes, memory_order_acquire);
    dap_io_shutdown(l_io);
    pthread_join(l_pth, NULL);
    double l_sec = (double)(dap_nanotime_now() - l_t0) / 1e9;
    s_print_result(a_nw, a_tasks, l_sec, a_expect, l_sent, l_acc.proc_bytes);
    dap_io_destroy(l_io); free(l_s); free(l_tids);
}

/* ================================================================== */
/*  Main                                                               */
/* ================================================================== */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IONBF, 0);

    g_data_pool = calloc(1, DATA_POOL_SIZE);
    memset(g_data_pool, 0xAB, DATA_POOL_SIZE);

    printf("=============================================================\n");
    printf("  SPSC lane throughput (typed API: batch_cb, dap_wfq_post)\n");
    printf("  Frames %d-%d B, consumer touches bytes, GB/s\n", FRAME_MIN, FRAME_MAX);
    printf("  Payload: dap_batch_task_t (%zuB)\n", sizeof(dap_batch_task_t));
    printf("=============================================================\n\n");

    unsigned l_cfgs[] = { 1, 2, 4, 8 };
    unsigned l_tasks = 200000;
    for (unsigned c = 0; c < sizeof(l_cfgs)/sizeof(l_cfgs[0]); ++c) {
        unsigned l_nw = l_cfgs[c];
        frame_schedule_t *l_sch = malloc(l_nw * sizeof(frame_schedule_t));
        uint64_t l_exp = 0;
        for (unsigned i = 0; i < l_nw; ++i) {
            l_sch[i] = s_gen_schedule(i, l_tasks);
            l_exp += l_sch[i].total_bytes;
        }
        printf("--- %u senders, %u tasks (%.1f MB total) ---\n",
               l_nw, l_tasks, (double)l_exp / (1024*1024));
        for (int r = 0; r < 3; ++r)
            s_run(l_nw, l_tasks, l_sch, l_exp);
        printf("\n");
        for (unsigned i = 0; i < l_nw; ++i) free(l_sch[i].descs);
        free(l_sch);
    }
    free(g_data_pool);
    return 0;
}
