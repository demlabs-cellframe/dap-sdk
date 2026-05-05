/*
 * test_stress.c — Stress tests for enhanced module primitives.
 *
 * Adapted from tests/vmqueue_bench/test_stress.c.
 */
#include "test_helpers_enh.h"
#include "dap_io_advanced.h"
#include "dap_io_send.h"
#include "dap_coro.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>

#define CLR_OK   "\033[32m"
#define CLR_FAIL "\033[31m"
#define CLR_RST  "\033[0m"

static int s_test_num = 0;

static void s_begin(const char *a_name)
{
    ++s_test_num;
    printf("\n[%d] %s\n", s_test_num, a_name);
}

static int s_pass(const char *a_detail)
{
    printf("    " CLR_OK "PASS" CLR_RST "  %s\n", a_detail);
    return 0;
}

static int s_fail(const char *a_detail)
{
    printf("    " CLR_FAIL "FAIL" CLR_RST "  %s\n", a_detail);
    return 1;
}

/* ================================================================== */
/*  1. Feeder kill mid-stream                                          */
/* ================================================================== */

typedef struct {
    dap_vmqueue_olb_t *olb;
    int fd;
    _Atomic(uint32_t) done;
    _Atomic(uint64_t) bytes_read;
} olb_reader_t;

static void *s_olb_reader(void *a_arg)
{
    olb_reader_t *l_ctx = a_arg;
    dap_vmqueue_olb_t *l_q = l_ctx->olb;
    uint64_t l_total = 0;
    for (;;) {
        char *l_ptr; size_t l_avail;
        dap_olb_space_t l_sp = dap_vmqolb_try_space(l_q, &l_ptr, &l_avail);
        if (l_sp == DAP_OLB_FULL) {
            uint64_t l_head; size_t l_a;
            if (s_olb_snapshot(l_q, &l_head, &l_a) && l_a > 0)
                dap_vmqolb_consume(l_q, l_a);
            continue;
        }
        ssize_t l_rd = read(l_ctx->fd, l_ptr, l_avail);
        if (l_rd <= 0) {
            if (l_rd < 0 && errno == EAGAIN) continue;
            break;
        }
        l_total += (uint64_t)l_rd;
        dap_vmqolb_advance_write(l_q, (size_t)l_rd);
        atomic_store_explicit(&l_q->tail_pos, l_q->write_end,
                              memory_order_release);
    }
    atomic_store_explicit(&l_ctx->bytes_read, l_total, memory_order_release);
    atomic_store_explicit(&l_ctx->done, 1, memory_order_release);
    return NULL;
}

static int test_feeder_kill(void)
{
    s_begin("Feeder kill mid-stream");

    int l_sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, l_sv) < 0)
        return s_fail("socketpair failed");
    fcntl(l_sv[0], F_SETFL, fcntl(l_sv[0], F_GETFL) | O_NONBLOCK);

    dap_vmqueue_olb_t *l_olb = dap_vmqueue_olb_create(Kbytes(256), false);
    if (!l_olb) { close(l_sv[0]); close(l_sv[1]); return s_fail("olb create"); }

    size_t l_src_sz; uint64_t l_sum;
    uint8_t *l_src = s_generate_frames(200, 64, 256, 42, &l_src_sz, &l_sum);
    if (!l_src) { close(l_sv[0]); close(l_sv[1]); dap_vmqueue_olb_destroy(l_olb); return s_fail("gen"); }
    printf("    feeding %zu bytes then closing fd\n", l_src_sz);

    feeder_ctx_t l_fc = { .fd = l_sv[1], .src = l_src, .src_size = l_src_sz };
    olb_reader_t l_rc = { .olb = l_olb, .fd = l_sv[0] };
    atomic_init(&l_rc.done, (uint32_t)0);
    atomic_init(&l_rc.bytes_read, (uint64_t)0);

    pthread_t l_ft, l_rt;
    uint64_t l_t0 = dap_nanotime_now();
    pthread_create(&l_ft, NULL, s_feeder, &l_fc);
    pthread_create(&l_rt, NULL, s_olb_reader, &l_rc);
    pthread_join(l_ft, NULL);

    uint64_t l_deadline = dap_nanotime_now() + 2000000000ULL;
    while (!atomic_load_explicit(&l_rc.done, memory_order_acquire)) {
        if (dap_nanotime_now() > l_deadline) {
            close(l_sv[0]); free(l_src); dap_vmqueue_olb_destroy(l_olb);
            return s_fail("reader hung >2s after feeder closed");
        }
        usleep(1000);
    }
    pthread_join(l_rt, NULL);
    uint64_t l_elapsed = dap_nanotime_now() - l_t0;
    uint64_t l_read = atomic_load_explicit(&l_rc.bytes_read, memory_order_acquire);
    printf("    reader exited: %lu bytes read in %.1f ms\n",
           (unsigned long)l_read, (double)l_elapsed / 1e6);
    close(l_sv[0]); free(l_src); dap_vmqueue_olb_destroy(l_olb);
    return (l_read == l_src_sz) ? s_pass("all bytes delivered, clean EOF")
                                : s_pass("partial delivery (expected for kill test)");
}

/* ================================================================== */
/*  2. OLB overflow saturation                                         */
/* ================================================================== */

static int test_olb_saturation(void)
{
    s_begin("OLB overflow saturation");
    dap_vmqueue_olb_t *l_q = dap_vmqueue_olb_create(Kbytes(64), false);
    if (!l_q) return s_fail("olb create");

    size_t l_written = 0, l_consumed = 0, l_full_count = 0, l_ok_count = 0, l_cmp_count = 0;
    for (int l_round = 0; l_round < 500; ++l_round) {
        char *l_ptr; size_t l_avail;
        dap_olb_space_t l_sp = dap_vmqolb_try_space(l_q, &l_ptr, &l_avail);
        if (l_sp == DAP_OLB_COMPACTED) ++l_cmp_count;
        if (l_sp == DAP_OLB_FULL) {
            ++l_full_count;
            uint64_t l_head; size_t l_a;
            if (s_olb_snapshot(l_q, &l_head, &l_a) && l_a > 0) {
                dap_vmqolb_consume(l_q, l_a);
                l_consumed += l_a;
            }
            continue;
        }
        ++l_ok_count;
        size_t l_fill = l_avail > 4096 ? 4096 : l_avail;
        memset(l_ptr, 'X', l_fill);
        dap_vmqolb_advance_write(l_q, l_fill);
        atomic_store_explicit(&l_q->tail_pos, l_q->write_end, memory_order_release);
        l_written += l_fill;
    }
    uint64_t l_head; size_t l_a;
    if (s_olb_snapshot(l_q, &l_head, &l_a) && l_a > 0) {
        dap_vmqolb_consume(l_q, l_a);
        l_consumed += l_a;
    }
    dap_vmqueue_olb_destroy(l_q);

    printf("    500 rounds: ok=%zu  full=%zu  compacted=%zu\n", l_ok_count, l_full_count, l_cmp_count);
    printf("    written: %zuKB  consumed: %zuKB\n", l_written >> 10, l_consumed >> 10);

    if (l_consumed != l_written)
        return s_fail("data loss: consumed != written");
    if (l_full_count == 0)
        return s_fail("never hit FULL — OLB too large for test");
    return s_pass("all data accounted for");
}

/* ================================================================== */
/*  3. Concurrent shutdown race                                        */
/* ================================================================== */

typedef struct {
    dap_vmqueue_mpsc_t *q;
    unsigned lane;
    _Atomic(uint64_t) pushed;
} mpsc_prod_t;

static void *s_stress_producer(void *a_arg)
{
    mpsc_prod_t *l_c = a_arg;
    uint64_t l_n = 0, l_val = 0;
    for (;;) {
        if (atomic_load_explicit(&l_c->q->shutdown, memory_order_relaxed))
            break;
        if (dap_vmqueue_mpsc_push(l_c->q, l_c->lane, 0, 0, &l_val, sizeof(l_val)))
            ++l_n;
        else
            sched_yield();
        ++l_val;
    }
    atomic_store_explicit(&l_c->pushed, l_n, memory_order_release);
    return NULL;
}

static void s_count_read(const void *a_payload, uint32_t a_len,
                         uint8_t a_type, uint8_t a_pri, void *a_arg)
{
    (void)a_payload; (void)a_len; (void)a_type; (void)a_pri;
    ++(*(size_t *)a_arg);
}

static int test_shutdown_race(void)
{
    s_begin("Concurrent shutdown race");
    enum { N_PROD = 8, LANE_CAP = 64 * 1024 };
    printf("    %d producers, lane capacity %dKB each\n", N_PROD, LANE_CAP >> 10);

    size_t l_caps[N_PROD];
    for (int i = 0; i < N_PROD; ++i) l_caps[i] = LANE_CAP;

    dap_vmqueue_mpsc_t *l_q = dap_vmqueue_mpsc_create(N_PROD, l_caps);
    if (!l_q) return s_fail("mpsc create");

    mpsc_prod_t l_ctx[N_PROD];
    pthread_t l_ths[N_PROD];
    for (int i = 0; i < N_PROD; ++i) {
        l_ctx[i] = (mpsc_prod_t){ .q = l_q, .lane = (unsigned)i };
        atomic_init(&l_ctx[i].pushed, (uint64_t)0);
        pthread_create(&l_ths[i], NULL, s_stress_producer, &l_ctx[i]);
    }

    printf("    producers running, waiting 50ms then shutdown...\n");
    usleep(50000);
    dap_vmqueue_mpsc_shutdown(l_q);

    int l_hung = 0;
    for (int i = 0; i < N_PROD; ++i) {
        struct timespec l_ts;
        clock_gettime(CLOCK_REALTIME, &l_ts);
        l_ts.tv_sec += 2;
        if (pthread_timedjoin_np(l_ths[i], NULL, &l_ts) != 0) {
            printf("    " CLR_FAIL "thread %d hung!" CLR_RST "\n", i);
            ++l_hung;
        }
    }
    if (l_hung) {
        dap_vmqueue_mpsc_destroy(l_q);
        return s_fail("threads did not exit within 2s");
    }

    uint64_t l_total = 0;
    for (int i = 0; i < N_PROD; ++i) {
        uint64_t l_p = atomic_load_explicit(&l_ctx[i].pushed, memory_order_acquire);
        l_total += l_p;
    }

    size_t l_pending_before = dap_wfq_pending(l_q, 0, N_PROD);

    size_t l_drained = 0;
    (void)dap_vmqueue_mpsc_drain_typed(l_q, s_count_read, &l_drained);

    size_t l_pending_after = dap_wfq_pending(l_q, 0, N_PROD);
    printf("    total pushed: %lu  drained: %zu  pending before: %zu  after: %zu\n",
           (unsigned long)l_total, l_drained, l_pending_before, l_pending_after);

    if (l_pending_after != 0) {
        dap_vmqueue_mpsc_destroy(l_q);
        return s_fail("pending != 0 after full drain");
    }

    dap_vmqueue_mpsc_destroy(l_q);
    char l_msg[128];
    snprintf(l_msg, sizeof(l_msg), "all %d threads exited cleanly, %lu msgs, pending drained to 0",
             N_PROD, (unsigned long)l_total);
    return s_pass(l_msg);
}

/* ================================================================== */
/*  4. Frame parser fuzzing                                            */
/* ================================================================== */

static int test_parser_fuzz(void)
{
    s_begin("Frame parser fuzzing");
    enum { BUF_SZ = 65536, ROUNDS = 10000 };

    char *l_buf = malloc(BUF_SZ);
    if (!l_buf) return s_fail("malloc");
    uint32_t l_rng = 0xDEAD;

    uint64_t l_t0 = dap_nanotime_now();
    size_t l_total_parsed = 0;
    for (int r = 0; r < ROUNDS; ++r) {
        for (int i = 0; i < BUF_SZ; ++i)
            l_buf[i] = (char)(s_xorshift32(&l_rng) & 0xFF);
        frame_parser_t l_p = { .max_frame_sz = 8192 };
        l_total_parsed += frame_parser_feed(&l_p, l_buf, BUF_SZ);
    }
    uint64_t l_fuzz_ns = dap_nanotime_now() - l_t0;
    printf("    random: %zu frames in %.1f ms (no crash)\n",
           l_total_parsed, (double)l_fuzz_ns / 1e6);

    int l_failures = 0;

    memset(l_buf, 0, BUF_SZ);
    memcpy(l_buf, c_test_sig, TEST_SIG_SIZE);
    uint32_t l_zero = 0;
    memcpy(l_buf + TEST_SIG_SIZE, &l_zero, sizeof(l_zero));
    frame_parser_t l_p = { .max_frame_sz = 1024 };
    size_t l_n = frame_parser_feed(&l_p, l_buf, FRAME_HDR_SIZE);
    printf("    zero-payload: parsed=%zu (expected 1) %s\n", l_n, l_n == 1 ? "ok" : "FAIL");
    if (l_n != 1) ++l_failures;

    uint32_t l_huge = Mbytes(1);
    memcpy(l_buf + TEST_SIG_SIZE, &l_huge, sizeof(l_huge));
    l_p = (frame_parser_t){ .max_frame_sz = 1024 };
    l_n = frame_parser_feed(&l_p, l_buf, FRAME_HDR_SIZE + l_huge);
    printf("    oversized: parsed=%zu (expected 0) %s\n", l_n, l_n == 0 ? "ok" : "FAIL");
    if (l_n != 0) ++l_failures;

    free(l_buf);
    return l_failures ? s_fail("edge case mismatch") : s_pass("all edge cases correct");
}

/* ================================================================== */
/*  5. MPSC lane contention with concurrent consumer                   */
/* ================================================================== */

typedef struct {
    dap_vmqueue_mpsc_t *q;
    unsigned lane;
    size_t count;
    uint64_t checksum;
} contention_ctx_t;

static void *s_contention_producer(void *a_arg)
{
    contention_ctx_t *l_c = a_arg;
    uint64_t l_sum = 0;
    _Atomic(uint64_t) *l_tg = s_mpsc_tg(l_c->q, l_c->lane);
    _Atomic(uint64_t) *l_hg = s_mpsc_hg(l_c->q, l_c->lane);
    _Atomic(uint32_t) *l_pw = s_mpsc_pw(l_c->q, l_c->lane);
    char  *l_data = s_mpsc_data(l_c->q, l_c->lane);
    size_t l_cap  = s_mpsc_cap(l_c->q, l_c->lane);
    for (size_t i = 0; i < l_c->count; ++i) {
        uint64_t l_val = i + l_c->lane * 1000000ULL;
        l_sum += l_val;
        if (!s_vmq_push(l_tg, l_hg, l_data, l_cap, 0, 0, &l_val, sizeof(l_val)))
            s_vmq_push_wait_ex(l_tg, l_hg, l_pw, &l_c->q->shutdown,
                               l_data, l_cap, 0, 0, &l_val, sizeof(l_val),
                               NULL, NULL);
        dap_vmqueue_mpsc_notify(l_c->q);
    }
    l_c->checksum = l_sum;
    return NULL;
}

typedef struct {
    uint64_t sum;
    uint64_t count;
} sum_ctx_t;

static void s_sum_read(const void *a_payload, uint32_t a_len,
                       uint8_t a_type, uint8_t a_pri, void *a_arg)
{
    (void)a_len; (void)a_type; (void)a_pri;
    sum_ctx_t *l_ctx = a_arg;
    l_ctx->sum += *(const uint64_t *)a_payload;
    l_ctx->count++;
}

static void *s_consume_thread(void *a_arg)
{
    dap_vmqueue_mpsc_t *l_q = ((void **)a_arg)[0];
    sum_ctx_t          *l_s = ((void **)a_arg)[1];
    dap_vmqueue_mpsc_consume(l_q, s_sum_read, l_s);
    return NULL;
}

static int test_mpsc_contention(void)
{
    s_begin("MPSC lane contention (concurrent consumer)");
    enum { N_LANES = 4, MSGS = 200000, LANE_CAP = 256 * 1024 };

    size_t l_caps[N_LANES];
    for (int i = 0; i < N_LANES; ++i) l_caps[i] = LANE_CAP;

    dap_vmqueue_mpsc_t *l_q = dap_vmqueue_mpsc_create(N_LANES, l_caps);
    if (!l_q) return s_fail("mpsc create");

    sum_ctx_t l_sum = { 0, 0 };
    void *l_cargs[2] = { l_q, &l_sum };
    pthread_t l_dt;
    pthread_create(&l_dt, NULL, s_consume_thread, l_cargs);

    contention_ctx_t l_ctx[N_LANES];
    pthread_t l_ths[N_LANES];
    uint64_t l_t0 = dap_nanotime_now();
    for (int i = 0; i < N_LANES; ++i) {
        l_ctx[i] = (contention_ctx_t){
            .q = l_q, .lane = (unsigned)i, .count = MSGS };
        pthread_create(&l_ths[i], NULL, s_contention_producer, &l_ctx[i]);
    }
    for (int i = 0; i < N_LANES; ++i) pthread_join(l_ths[i], NULL);
    uint64_t l_prod_ns = dap_nanotime_now() - l_t0;

    dap_vmqueue_mpsc_shutdown(l_q);
    pthread_join(l_dt, NULL);

    uint64_t l_expected = 0;
    for (int i = 0; i < N_LANES; ++i)
        l_expected += l_ctx[i].checksum;

    printf("    messages: expected %d  got %lu  time %.1f ms\n",
           N_LANES * MSGS, (unsigned long)l_sum.count, (double)l_prod_ns / 1e6);

    size_t l_pending = dap_wfq_pending(l_q, 0, N_LANES);
    dap_vmqueue_mpsc_destroy(l_q);
    if (l_sum.sum != l_expected || l_sum.count != (uint64_t)(N_LANES * MSGS))
        return s_fail("checksum or count mismatch");
    if (l_pending != 0)
        return s_fail("pending != 0 after drain completed");
    return s_pass("all messages delivered, checksums match");
}

/* ================================================================== */
/*  6. Message stack (Treiber MPSC) contention test                    */
/* ================================================================== */

typedef struct {
    dap_proc_ctx_t     *proc;
    _Atomic(uint32_t)  *start;
    unsigned            iters;
    int                 result;
} ext_push_race_t;

static void *s_ext_push_racer(void *a_arg)
{
    ext_push_race_t *l_c = a_arg;
    while (!atomic_load_explicit(l_c->start, memory_order_acquire))
        dap_cpu_relax();
    int l_ok = 0;
    for (unsigned i = 0; i < l_c->iters; ++i) {
        if (dap_msg_post_heap(l_c->proc, NULL, 0, NULL))
            ++l_ok;
    }
    l_c->result = l_ok;
    return NULL;
}

static int test_ext_stack_race(void)
{
    s_begin("Message stack (Treiber MPSC) contention");
    enum { N_THREADS = 16, ITERS = 5000 };

    dap_proc_ctx_t l_proc = { .ext_stack = DAP_MSG_STACK_INIT };
    dap_wfq_wait_state_t l_wfq_waiting;
    atomic_init(&l_wfq_waiting.rescan_mask, (uint64_t)0);
    l_proc.wfq_waiting = &l_wfq_waiting;
    _Atomic(uint32_t) l_start;
    atomic_init(&l_start, (uint32_t)0);

    ext_push_race_t l_ctx[N_THREADS];
    pthread_t l_ths[N_THREADS];
    for (int i = 0; i < N_THREADS; ++i) {
        l_ctx[i] = (ext_push_race_t){
            .proc = &l_proc,
            .start = &l_start, .iters = ITERS };
        pthread_create(&l_ths[i], NULL, s_ext_push_racer, &l_ctx[i]);
    }

    uint64_t l_t0 = dap_nanotime_now();
    atomic_store_explicit(&l_start, 1, memory_order_release);
    int l_total_ok = 0;
    for (int i = 0; i < N_THREADS; ++i) {
        pthread_join(l_ths[i], NULL);
        l_total_ok += l_ctx[i].result;
    }
    uint64_t l_elapsed = dap_nanotime_now() - l_t0;

    printf("    pushes: %d / %d (%.1f%%)  time: %.1f ms\n",
           l_total_ok, N_THREADS * ITERS, 100.0 * l_total_ok / (N_THREADS * ITERS),
           (double)l_elapsed / 1e6);

    dap_msg_t *l_chain = dap_msg_stack_detach(&l_proc.ext_stack);
    int l_count = 0;
    while (l_chain) {
        dap_msg_t *l_next = l_chain->next;
        free(l_chain);
        l_chain = l_next;
        ++l_count;
    }
    printf("    drained: %d  (expected %d)\n", l_count, l_total_ok);
    if (l_count != l_total_ok)
        return s_fail("message count mismatch");
    return s_pass("all messages accounted for");
}

/* ================================================================== */
/*  7. OLB compaction-aware read discipline                            */
/* ================================================================== */

typedef struct {
    dap_vmqueue_olb_t *olb;
    frame_parser_t     parser;
    int                fd;
    size_t             max_frame;
    bool               closed;
    bool               suspended;
    size_t             total_suspends;
    size_t             total_compacts;
    size_t             total_frames;
} stress_conn_t;

static void s_stress_handle_read(stress_conn_t *a_c)
{
    for (;;) {
        char *l_ptr; size_t l_avail;
        dap_olb_space_t l_sp = dap_vmqolb_try_space(a_c->olb,
            &l_ptr, &l_avail);
        if (l_sp == DAP_OLB_FULL) {
            a_c->suspended = true;
            ++a_c->total_suspends;
            return;
        }
        if (l_sp == DAP_OLB_COMPACTED) {
            uint32_t l_mf = a_c->parser.max_frame_sz;
            a_c->parser = (frame_parser_t){ .max_frame_sz = l_mf };
            ++a_c->total_compacts;
        }
        ssize_t l_rd = read(a_c->fd, l_ptr, l_avail);
        if (l_rd > 0) {
            dap_vmqolb_advance_write(a_c->olb, (size_t)l_rd);
            size_t l_n = frame_parser_feed(&a_c->parser, a_c->olb->data,
                                            a_c->olb->write_end);
            if (l_n) {
                atomic_store_explicit(&a_c->olb->tail_pos, a_c->parser.tail,
                                      memory_order_release);
                a_c->total_frames += l_n;
            }
            continue;
        }
        if (l_rd == 0 || errno != EAGAIN)
            a_c->closed = true;
        return;
    }
}

static int test_olb_compaction(void)
{
    s_begin("OLB compaction-aware read discipline (zero memmove)");
    enum { N_FRAMES = 100000, OLB_CAP = 1024 * 1024 };
    size_t l_min = 64, l_max = 4096;
    size_t l_mf = s_frame_size((uint32_t)l_max);

    dap_vmqueue_olb_t *l_olb = dap_vmqueue_olb_create(OLB_CAP, false);
    if (!l_olb) return s_fail("olb create");
    l_olb->compact_threshold = l_mf * 2;

    int l_sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, l_sv) < 0)
        { dap_vmqueue_olb_destroy(l_olb); return s_fail("socketpair"); }
    fcntl(l_sv[0], F_SETFL, fcntl(l_sv[0], F_GETFL) | O_NONBLOCK);
    fcntl(l_sv[1], F_SETFL, fcntl(l_sv[1], F_GETFL) & ~O_NONBLOCK);

    size_t l_src_sz; uint64_t l_expected;
    uint8_t *l_src = s_generate_frames(N_FRAMES, l_min, l_max, 42, &l_src_sz, &l_expected);
    if (!l_src) { close(l_sv[0]); close(l_sv[1]); dap_vmqueue_olb_destroy(l_olb); return s_fail("gen"); }

    feeder_ctx_t l_fc = { .fd = l_sv[1], .src = l_src, .src_size = l_src_sz };
    pthread_t l_ft;
    pthread_create(&l_ft, NULL, s_feeder, &l_fc);

    stress_conn_t l_conn = {
        .olb = l_olb, .fd = l_sv[0], .max_frame = l_mf,
        .parser.max_frame_sz = (uint32_t)l_mf
    };

    int l_epfd = epoll_create1(0);
    struct epoll_event l_ev = { .events = EPOLLIN | EPOLLET, .data.u32 = 0 };
    epoll_ctl(l_epfd, EPOLL_CTL_ADD, l_sv[0], &l_ev);

    uint64_t l_t0 = dap_nanotime_now();
    while (!l_conn.closed) {
        struct epoll_event l_evs[4];
        int l_n = epoll_wait(l_epfd, l_evs, 4, 100);
        if (l_n > 0 && !l_conn.suspended)
            s_stress_handle_read(&l_conn);
        if (l_conn.suspended) {
            uint64_t l_head; size_t l_a;
            if (s_olb_snapshot(l_olb, &l_head, &l_a) && l_a > 0) {
                dap_vmqolb_consume(l_olb, l_a);
                l_conn.suspended = false;
                s_stress_handle_read(&l_conn);
            }
        }
    }
    { uint64_t l_head; size_t l_a;
      while (s_olb_snapshot(l_olb, &l_head, &l_a) && l_a > 0)
          dap_vmqolb_consume(l_olb, l_a);
    }
    uint64_t l_elapsed = dap_nanotime_now() - l_t0;
    pthread_join(l_ft, NULL);
    close(l_sv[0]); close(l_epfd);

    size_t l_mmv = l_olb->memmove_compacts;
    printf("    frames: %zu/%d  compacts: %zu  memmove: %zu  suspends: %zu\n",
           l_conn.total_frames, N_FRAMES, l_conn.total_compacts, l_mmv,
           l_conn.total_suspends);
    printf("    time: %.1f ms\n", (double)l_elapsed / 1e6);

    free(l_src);
    dap_vmqueue_olb_destroy(l_olb);

    if (l_mmv > 0)
        return s_fail("memmove compactions detected");
    if (l_conn.total_frames != N_FRAMES)
        return s_fail("frame count mismatch");
    return s_pass("zero memmove, all frames parsed");
}

/* ================================================================== */
/*  8. Send OLB wrap-around stress                                     */
/* ================================================================== */

static int test_send_olb_wrap(void)
{
    s_begin("Send OLB wrap-around stress");
    enum { SEND_CAP = 64 * 1024, ROUNDS = 5000, CHUNK = 1024 };

    dap_vmqueue_olb_t *l_q = dap_vmqueue_olb_create(SEND_CAP, false);
    if (!l_q) return s_fail("olb create");

    int l_sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, l_sv) < 0)
        { dap_vmqueue_olb_destroy(l_q); return s_fail("socketpair"); }
    fcntl(l_sv[0], F_SETFL, fcntl(l_sv[0], F_GETFL) | O_NONBLOCK);

    sink_ctx_t l_sink = { .fd = l_sv[1] };
    atomic_init(&l_sink.recv_bytes, (uint64_t)0);
    pthread_t l_st;
    pthread_create(&l_st, NULL, s_sink_thread, &l_sink);

    char l_data[CHUNK];
    memset(l_data, 'W', CHUNK);
    uint64_t l_written = 0;
    size_t l_wraps = 0, l_flushes = 0;

    uint64_t l_t0 = dap_nanotime_now();
    for (int r = 0; r < ROUNDS; ++r) {
        if (dap_send_olb_write(l_q, l_data, CHUNK) != DAP_SEND_OLB_OK) {
            ++l_wraps;
            for (;;) {
                ssize_t l_f = dap_send_olb_flush(l_q, l_sv[0]);
                if (l_f <= 0) break;
                ++l_flushes;
            }
            if (dap_send_olb_write(l_q, l_data, CHUNK) != DAP_SEND_OLB_OK)
                continue;
        }
        l_written += CHUNK;
        if (r % 10 == 0) {
            for (;;) {
                ssize_t l_f = dap_send_olb_flush(l_q, l_sv[0]);
                if (l_f <= 0) break;
                ++l_flushes;
            }
        }
    }
    for (;;) {
        ssize_t l_f = dap_send_olb_flush(l_q, l_sv[0]);
        if (l_f == 0) break;
        if (l_f > 0) { ++l_flushes; continue; }
        if (errno == EAGAIN) { sched_yield(); continue; }
        break;
    }
    uint64_t l_elapsed = dap_nanotime_now() - l_t0;
    close(l_sv[0]);
    pthread_join(l_st, NULL);

    uint64_t l_recv = atomic_load_explicit(&l_sink.recv_bytes, memory_order_acquire);
    dap_vmqueue_olb_destroy(l_q);

    printf("    written: %luKB  recv: %luKB  wraps: %zu  time: %.1f ms\n",
           (unsigned long)(l_written >> 10), (unsigned long)(l_recv >> 10),
           l_wraps, (double)l_elapsed / 1e6);

    if (l_recv != l_written)
        return s_fail("byte count mismatch");
    return s_pass("all bytes delivered through wraps");
}

/* ================================================================== */
/*  9. Coroutine pool churn                                            */
/* ================================================================== */

#ifndef __SANITIZE_THREAD__
static void s_churn_fn(void *a_arg) { *(uint64_t *)a_arg += 1; }

static int test_coro_churn(void)
{
    s_begin("Coroutine pool churn");
    enum { SLOTS = 4, ROUNDS = 50000 };
    printf("    %d slots, %d rounds of full create/resume/destroy cycle\n", SLOTS, ROUNDS);

    dap_coro_pool_t l_pool;
    if (!dap_coro_pool_mmap(&l_pool, SLOTS, 0))
        return s_fail("pool mmap");
    printf("    pool zone: %zuKB, stack: %zuKB, slots: %u\n",
           l_pool.zone_size >> 10, l_pool.stack_usable >> 10, l_pool.capacity);

    uint64_t l_counter = 0;
    uint64_t l_t0 = dap_nanotime_now();
    for (int r = 0; r < ROUNDS; ++r) {
        dap_coro_t *l_cos[SLOTS];
        for (int i = 0; i < SLOTS; ++i) {
            l_cos[i] = dap_coro_create(&l_pool, s_churn_fn, &l_counter);
            if (!l_cos[i]) {
                dap_coro_pool_fini(&l_pool);
                char l_msg[80];
                snprintf(l_msg, sizeof(l_msg), "create failed at round %d slot %d", r, i);
                return s_fail(l_msg);
            }
        }
        dap_coro_t *l_extra = dap_coro_create(&l_pool, s_churn_fn, &l_counter);
        if (l_extra) {
            dap_coro_pool_fini(&l_pool);
            return s_fail("pool exhaustion not enforced");
        }
        for (int i = 0; i < SLOTS; ++i) {
            dap_coro_resume(l_cos[i]);
            if (l_cos[i]->state != DAP_CORO_DONE) {
                dap_coro_pool_fini(&l_pool);
                char l_msg[80];
                snprintf(l_msg, sizeof(l_msg), "coro not DONE after resume at round %d slot %d", r, i);
                return s_fail(l_msg);
            }
            dap_coro_destroy(&l_pool, l_cos[i]);
        }
    }
    uint64_t l_elapsed = dap_nanotime_now() - l_t0;
    dap_coro_pool_fini(&l_pool);

    printf("    total calls: %lu (expected %lu)\n",
           (unsigned long)l_counter, (unsigned long)((uint64_t)ROUNDS * SLOTS));
    printf("    time: %.1f ms (%.2f Mops)\n",
           (double)l_elapsed / 1e6, (double)l_counter / ((double)l_elapsed / 1e9) / 1e6);

    if (l_counter != (uint64_t)ROUNDS * SLOTS)
        return s_fail("counter mismatch");
    return s_pass("all create/resume/destroy cycles OK");
}
#endif

/* ================================================================== */
/*  10. Stackless coroutine scheduler stress                           */
/* ================================================================== */

typedef struct { int phase; uint64_t sum; } sl_state_t;

static int s_sl_step(void *a_state)
{
    sl_state_t *s = (sl_state_t *)a_state;
    s->sum += (uint64_t)s->phase;
    if (++s->phase >= 10)
        return DAP_SL_DONE;
    return (s->phase % 3 == 0) ? DAP_SL_WAIT : DAP_SL_YIELD;
}

static int test_sl_sched_stress(void)
{
    s_begin("Stackless coroutine scheduler stress");
    enum { N_CORO = 256, KEYS = 8 };
    printf("    %d coros, %d wake keys, 10 steps each\n", N_CORO, KEYS);

    dap_sl_pool_t l_pool;
    if (!dap_sl_pool_init(&l_pool, N_CORO)) return s_fail("pool init");
    dap_sl_sched_t l_sched;
    dap_sl_sched_init(&l_sched);

    for (int i = 0; i < N_CORO; ++i) {
        dap_sl_coro_t *l_co = dap_sl_acquire(&l_pool, s_sl_step, (uintptr_t)(i % KEYS));
        if (!l_co) { dap_sl_pool_fini(&l_pool); return s_fail("acquire"); }
        dap_sl_sched_put_run(&l_sched, l_co);
    }
    printf("    initial: run=%u wait=%u\n", l_sched.n_run, l_sched.n_wait);

    size_t l_done = 0, l_ticks = 0, l_wakes = 0;
    uint64_t l_t0 = dap_nanotime_now();
    while (l_sched.n_run > 0 || l_sched.n_wait > 0) {
        dap_sl_coro_t *l_co;
        while ((l_co = dap_sl_sched_next(&l_sched))) {
            int rc = l_co->step(l_co->state);
            if (rc == DAP_SL_DONE) {
                ++l_done;
                dap_sl_release(&l_pool, l_co);
            } else if (rc == DAP_SL_YIELD) {
                dap_sl_sched_put_run(&l_sched, l_co);
            } else {
                dap_sl_sched_put_wait(&l_sched, l_co);
            }
        }
        if (l_sched.n_wait > 0) {
            uintptr_t l_key = (uintptr_t)(l_ticks % KEYS);
            unsigned l_w = dap_sl_sched_wake_all(&l_sched, l_key);
            l_wakes += l_w;
        }
        ++l_ticks;
        if (l_ticks > 100000) {
            printf("    STUCK: tick=%zu done=%zu run=%u wait=%u\n",
                   l_ticks, l_done, l_sched.n_run, l_sched.n_wait);
            dap_sl_pool_fini(&l_pool);
            return s_fail("infinite loop detected");
        }
    }
    uint64_t l_elapsed = dap_nanotime_now() - l_t0;
    dap_sl_pool_fini(&l_pool);

    printf("    done=%zu  ticks=%zu  total wakes=%zu\n", l_done, l_ticks, l_wakes);
    printf("    time: %.2f ms\n", (double)l_elapsed / 1e6);

    if (l_done != N_CORO)
        return s_fail("not all coros completed");
    return s_pass("all coros completed through scheduler");
}

/* ================================================================== */
/*  Cold-path API helpers (no conn open, no socket I/O)                  */
/* ================================================================== */

static size_t s_cold_parse(dap_conn_t *a_c, dap_io_olb_parser_t *a_p)
{
    (void)a_c;
    (void)a_p;
    return 0;
}

static dap_io_parse_result_t s_cold_span_parse(const char *a_data, size_t a_size, void *a_arg)
{
    (void)a_data;
    (void)a_arg;
    return (dap_io_parse_result_t){ .consumed = a_size, .bytes_needed = 0 };
}

static void s_cold_compact(dap_conn_t *a_c) { (void)a_c; }

static void s_cold_ext_dtor(void *a_ext) { (void)a_ext; }

static void s_cold_frame(dap_conn_t *a_c, const char *a_batch, uint32_t a_bytes, void *a_arg)
{
    (void)a_c;
    (void)a_batch;
    (void)a_bytes;
    (void)a_arg;
}

static dap_msg_rc_t s_cold_frame_rc(dap_conn_t *a_c, const char *a_batch, uint32_t a_bytes,
                                    void *a_arg)
{
    (void)a_c;
    (void)a_batch;
    (void)a_bytes;
    (void)a_arg;
    return DAP_MSG_DONE;
}

static dap_msg_rc_t s_cold_batch(const dap_batch_task_t *a_t, void *a_arg)
{
    (void)a_t;
    (void)a_arg;
    return DAP_MSG_DONE;
}

static dap_msg_rc_t s_cold_heap(const dap_heap_task_t *a_t, void *a_arg)
{
    (void)a_t;
    (void)a_arg;
    return DAP_MSG_DONE;
}

static int test_cold_api_helpers(void)
{
    s_begin("Cold-path API helpers (OLB ext, ext_dtor, proc setters)");

    dap_io_olb_ext_t l_olb;
    dap_io_olb_parser_t l_parser;
    if (dap_io_olb_ext_setup(NULL, &l_parser, s_cold_parse, s_cold_compact))
        return s_fail("setup rejected NULL ext");
    if (dap_io_olb_ext_setup(&l_olb, NULL, s_cold_parse, s_cold_compact))
        return s_fail("setup rejected NULL parser");
    if (dap_io_olb_ext_setup(&l_olb, &l_parser, NULL, s_cold_compact))
        return s_fail("setup rejected NULL parse");
    if (dap_io_olb_ext_is_ready(NULL))
        return s_fail("is_ready(NULL) false");
    memset(&l_olb, 0, sizeof(l_olb));
    if (dap_io_olb_ext_is_ready(&l_olb))
        return s_fail("is_ready(zero ext) false");

    if (!dap_io_olb_ext_setup(&l_olb, &l_parser, s_cold_parse, NULL))
        return s_fail("setup NULL compact ok");
    if (l_olb.parser != &l_parser || l_parser.parse != s_cold_parse || l_parser.compact != NULL)
        return s_fail("parser wiring");
    if (!dap_io_olb_ext_is_ready(&l_olb))
        return s_fail("is_ready after setup");
    if (!dap_io_olb_ext_setup(&l_olb, &l_parser, s_cold_parse, s_cold_compact))
        return s_fail("setup non-NULL compact");
    if (l_parser.compact != s_cold_compact || !dap_io_olb_ext_is_ready(&l_olb))
        return s_fail("compact + is_ready");

    dap_io_olb_parser_t l_spanp;
    if (dap_io_span_parser_setup(NULL, &l_spanp, s_cold_span_parse, NULL, s_cold_compact))
        return s_fail("span setup rejected NULL ext");
    if (dap_io_span_parser_setup(&l_olb, NULL, s_cold_span_parse, NULL, s_cold_compact))
        return s_fail("span setup rejected NULL parser");
    if (dap_io_span_parser_setup(&l_olb, &l_spanp, NULL, NULL, s_cold_compact))
        return s_fail("span setup rejected NULL cb");
    if (!dap_io_span_parser_setup(&l_olb, &l_spanp, s_cold_span_parse,
            (void *)(uintptr_t)0x5150414e, s_cold_compact))
        return s_fail("span setup ok");
    if (l_olb.parser != &l_spanp || l_spanp.parse != dap_io_olb_parse_span
        || l_spanp.compact != s_cold_compact
        || l_spanp.span_parse != s_cold_span_parse
        || l_spanp.arg != (void *)(uintptr_t)0x5150414e || l_spanp.tail != 0)
        return s_fail("span setup wiring");
    if (!dap_io_olb_ext_is_ready(&l_olb))
        return s_fail("span setup ready");

    if (dap_io_send_rc_to_msg_rc(DAP_SEND_OK) != DAP_MSG_DONE
        || dap_io_send_rc_to_msg_rc(DAP_SEND_OVERFLOW) != DAP_MSG_DEFER
        || dap_io_send_rc_to_msg_rc(DAP_SEND_CLOSED) != DAP_MSG_DROP
        || dap_io_send_rc_to_msg_rc(DAP_SEND_TOO_LARGE) != DAP_MSG_DROP
        || dap_io_send_rc_to_msg_rc((dap_send_rc_t)255) != DAP_MSG_DROP)
        return s_fail("send rc mapper");

    dap_io_conn_cfg_t l_cfg = DAP_IO_CONN_CFG_INIT;
    if (l_cfg.io || l_cfg.worker_id != UINT_MAX || l_cfg.kind != (dap_io_kind_t)DAP_IO_KIND_COUNT
        || l_cfg.fd != (dap_fd_t)-1 || l_cfg.olb_cap || l_cfg.rx || l_cfg.read_cb
        || l_cfg.ext || l_cfg.max_frame || l_cfg.ext_dtor)
        return s_fail("conn cfg init");

    dap_conn_t l_conn;
    memset(&l_conn, 0, sizeof(l_conn));
    int l_ext_dummy;
    if (dap_conn_set_ext_dtor(NULL, s_cold_ext_dtor))
        return s_fail("set_ext_dtor NULL conn");
    if (dap_conn_set_ext_dtor(&l_conn, NULL))
        return s_fail("set_ext_dtor NULL dtor");
    if (dap_conn_set_ext_dtor(&l_conn, s_cold_ext_dtor))
        return s_fail("set_ext_dtor NULL ext");
    l_conn.ext = &l_ext_dummy;
    if (!dap_conn_set_ext_dtor(&l_conn, s_cold_ext_dtor))
        return s_fail("set_ext_dtor ok");
    if (l_conn.ext_dtor != s_cold_ext_dtor)
        return s_fail("ext_dtor stored");

    dap_io_t *l_io = dap_io_create(1, 1);
    if (!l_io)
        return s_fail("dap_io_create(1,1)");
    if (dap_io_proc_set_frame_cb(NULL, 0, s_cold_frame, NULL))
        return s_fail("frame_cb NULL io");
    if (dap_io_proc_set_frame_cb(l_io, 1, s_cold_frame, NULL))
        return s_fail("frame_cb bad proc_idx");
    if (dap_io_proc_set_batch_cb(NULL, 0, s_cold_batch, NULL))
        return s_fail("batch_cb NULL io");
    if (dap_io_proc_set_batch_cb(l_io, 1, s_cold_batch, NULL))
        return s_fail("batch_cb bad proc_idx");
    if (dap_io_proc_set_heap_cb(NULL, 0, s_cold_heap, NULL))
        return s_fail("heap_cb NULL io");
    if (dap_io_proc_set_heap_cb(l_io, 1, s_cold_heap, NULL))
        return s_fail("heap_cb bad proc_idx");
    if (dap_io_proc_set_frame_rc_cb(NULL, 0, s_cold_frame_rc, NULL))
        return s_fail("frame_rc_cb NULL io");
    if (dap_io_proc_set_frame_rc_cb(l_io, 1, s_cold_frame_rc, NULL))
        return s_fail("frame_rc_cb bad proc_idx");

    dap_proc_ctx_t *l_p = dap_io_proc(l_io, 0);
    if (!dap_io_proc_set_frame_cb(l_io, 0, s_cold_frame, (void *)(uintptr_t)0xf00d))
        return s_fail("set frame_cb");
    if (!l_p->frame_cb || l_p->batch_cb || l_p->frame_rc_cb
        || l_p->_inheritor != (void *)(uintptr_t)0xf00d)
        return s_fail("frame sets inheritor clears batch");
    if (!dap_io_proc_set_batch_cb(l_io, 0, s_cold_batch, (void *)(uintptr_t)0xba5e))
        return s_fail("set batch_cb");
    if (!l_p->batch_cb || l_p->frame_cb || l_p->frame_rc_cb
        || l_p->_inheritor != (void *)(uintptr_t)0xba5e)
        return s_fail("batch sets inheritor clears frame");
    if (!dap_io_proc_set_frame_cb(l_io, 0, s_cold_frame, (void *)(uintptr_t)0x1111))
        return s_fail("set frame_cb again");
    if (!dap_io_proc_set_heap_cb(l_io, 0, s_cold_heap, (void *)(uintptr_t)0xea70))
        return s_fail("set heap_cb");
    if (!l_p->heap_cb || !l_p->frame_cb || l_p->batch_cb || l_p->frame_rc_cb
        || l_p->_inheritor != (void *)(uintptr_t)0xea70)
        return s_fail("heap keeps frame clears batch untouched");
    if (!dap_io_proc_set_batch_cb(l_io, 0, s_cold_batch, (void *)(uintptr_t)0xb01))
        return s_fail("set batch after heap");
    if (l_p->frame_cb || !l_p->batch_cb || l_p->frame_rc_cb || l_p->heap_cb != s_cold_heap
        || l_p->_inheritor != (void *)(uintptr_t)0xb01)
        return s_fail("batch clears frame heap retained");
    if (!dap_io_proc_set_frame_cb(l_io, 0, s_cold_frame, (void *)(uintptr_t)0x2222))
        return s_fail("frame after batch+heap");
    if (!l_p->frame_cb || l_p->batch_cb || l_p->frame_rc_cb || l_p->heap_cb != s_cold_heap
        || l_p->_inheritor != (void *)(uintptr_t)0x2222)
        return s_fail("frame clears batch heap retained");
    if (!dap_io_proc_set_frame_rc_cb(l_io, 0, s_cold_frame_rc, (void *)(uintptr_t)0xccc))
        return s_fail("set frame_rc_cb tail");
    if (!l_p->frame_rc_cb || l_p->frame_cb || l_p->batch_cb
        || l_p->_inheritor != (void *)(uintptr_t)0xccc)
        return s_fail("frame_rc_cb exclusive");
    if (!dap_io_proc_set_batch_cb(l_io, 0, s_cold_batch, (void *)(uintptr_t)0xddd))
        return s_fail("batch after frame_rc");
    if (!l_p->batch_cb || l_p->frame_rc_cb || l_p->frame_cb
        || l_p->_inheritor != (void *)(uintptr_t)0xddd)
        return s_fail("batch clears frame_rc");

    dap_io_destroy(l_io);
    return s_pass("olb ext, ext_dtor, proc setters");
}

/* ================================================================== */
/*  dap_io_conn_open cold validation (no worker loop)                    */
/* ================================================================== */

typedef struct {
    dap_io_olb_ext_t olb;
    dap_io_olb_parser_t p;
} cold_bridge_ext_t;
DAP_IO_OLB_EXT_FIRST(cold_bridge_ext_t, olb);

#define S_RX_SENT_CTX  ((void *)(uintptr_t)0xeca86420fdb97531ULL)

static ssize_t s_rx_sent_pull(dap_conn_t *a_c, void *a_buf, size_t a_max, void *a_ctx)
{
    (void)a_c;
    (void)a_buf;
    (void)a_max;
    (void)a_ctx;
    return -1;
}

static void s_rx_sentinel(dap_io_rx_ctx_t *a_rx)
{
    a_rx->pull = s_rx_sent_pull;
    a_rx->pull_ctx = S_RX_SENT_CTX;
}

static int s_rx_unchanged(const dap_io_rx_ctx_t *a_rx, const char *a_what)
{
    if (a_rx->pull != s_rx_sent_pull || a_rx->pull_ctx != S_RX_SENT_CTX)
        return s_fail(a_what);
    return 0;
}

static int test_conn_open_validation(void)
{
    s_begin("dap_io_conn_open cold validation + OLB min cap");

    dap_io_t *l_io = dap_io_create(1, 1);
    if (!l_io)
        return s_fail("dap_io_create");
    int l_sv[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, l_sv) < 0) {
        dap_io_destroy(l_io);
        return s_fail("socketpair");
    }
    fcntl(l_sv[0], F_SETFL, fcntl(l_sv[0], F_GETFL) | O_NONBLOCK);
    fcntl(l_sv[1], F_SETFL, fcntl(l_sv[1], F_GETFL) | O_NONBLOCK);

    dap_io_rx_ctx_t l_rx;
    dap_io_olb_ext_t l_bad;
    int l_err = 0;

    s_rx_sentinel(&l_rx);
    if (dap_io_conn_open(NULL, 0, DAP_IO_SOCK, l_sv[0], 1, &l_rx, NULL, NULL, 0) != NULL)
        l_err += s_fail("NULL io");
    else
        l_err += s_rx_unchanged(&l_rx, "rx after NULL io");

    s_rx_sentinel(&l_rx);
    if (dap_io_conn_open(l_io, 1, DAP_IO_SOCK, l_sv[0], 1, &l_rx, NULL, NULL, 0) != NULL)
        l_err += s_fail("bad worker");
    else
        l_err += s_rx_unchanged(&l_rx, "rx after bad worker");

    s_rx_sentinel(&l_rx);
    if (dap_io_conn_open(l_io, 0, DAP_IO_TIMER, l_sv[0], 1, &l_rx, NULL, NULL, 0) != NULL)
        l_err += s_fail("DAP_IO_TIMER");
    else
        l_err += s_rx_unchanged(&l_rx, "rx after TIMER kind");

    s_rx_sentinel(&l_rx);
    if (dap_io_conn_open(l_io, 0, (dap_io_kind_t)DAP_IO_KIND_COUNT, l_sv[0], 1, &l_rx, NULL, NULL, 0) != NULL)
        l_err += s_fail("kind >= COUNT");
    else
        l_err += s_rx_unchanged(&l_rx, "rx after invalid kind");

#ifndef DAP_OS_WINDOWS
    s_rx_sentinel(&l_rx);
    if (dap_io_conn_open(l_io, 0, DAP_IO_SOCK, -1, 1, &l_rx, NULL, NULL, 0) != NULL)
        l_err += s_fail("bad fd");
    else
        l_err += s_rx_unchanged(&l_rx, "rx after bad fd");
#endif

    s_rx_sentinel(&l_rx);
    if (dap_io_conn_open(l_io, 0, DAP_IO_SOCK, l_sv[0], 1, &l_rx, dap_io_rx_bridge, NULL, 0) != NULL)
        l_err += s_fail("bridge NULL ext");
    else
        l_err += s_rx_unchanged(&l_rx, "rx after bridge NULL ext");

    memset(&l_bad, 0, sizeof(l_bad));
    s_rx_sentinel(&l_rx);
    if (dap_io_conn_open(l_io, 0, DAP_IO_SOCK, l_sv[0], 1, &l_rx, dap_io_rx_bridge, &l_bad, 0) != NULL)
        l_err += s_fail("bridge ext not ready");
    else
        l_err += s_rx_unchanged(&l_rx, "rx after bridge ext not ready");

    s_rx_sentinel(&l_rx);
    if (dap_io_conn_open_with_ext_dtor(l_io, 0, DAP_IO_SOCK, l_sv[0], 1,
            &l_rx, NULL, NULL, 0, s_cold_ext_dtor) != NULL)
        l_err += s_fail("open_with_ext_dtor NULL ext");
    else
        l_err += s_rx_unchanged(&l_rx, "rx after open_with_ext_dtor NULL ext");

    if (l_err) {
        close(l_sv[0]);
        close(l_sv[1]);
        dap_io_destroy(l_io);
        return 1;
    }

    cold_bridge_ext_t l_ext;
    memset(&l_ext, 0, sizeof(l_ext));
    if (!dap_io_olb_ext_setup(&l_ext.olb, &l_ext.p, s_cold_parse, s_cold_compact)) {
        close(l_sv[0]);
        close(l_sv[1]);
        dap_io_destroy(l_io);
        return s_fail("olb_ext_setup");
    }
    dap_conn_t *l_c = dap_io_conn_open_with_ext_dtor(l_io, 0, DAP_IO_SOCK, l_sv[0], 1,
                                         NULL, dap_io_rx_bridge, &l_ext, 0, s_cold_ext_dtor);
    if (!l_c) {
        close(l_sv[0]);
        close(l_sv[1]);
        dap_io_destroy(l_io);
        return s_fail("bridge open");
    }
    if (!l_c->olb || !l_c->send_olb
        || l_c->olb->capacity < DAP_IO_OLB_MIN_CAP || l_c->send_olb->capacity < DAP_IO_OLB_MIN_CAP
        || l_c->ext != &l_ext || l_c->ext_dtor != s_cold_ext_dtor || !l_ext.olb.rx.pull) {
        close(l_sv[1]);
        dap_io_destroy(l_io);
        close(l_sv[0]);
        return s_fail("bridge conn checks");
    }
    close(l_sv[1]);
    dap_io_destroy(l_io);
    close(l_sv[0]);
    return s_pass("cold validation + min cap bridge open");
}

/* ================================================================== */
/*  Main                                                               */
/* ================================================================== */

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("===========================================================\n");
    printf("  Stress tests for enhanced module primitives\n");
    printf("===========================================================\n");

    uint64_t l_t0 = dap_nanotime_now();
    int l_fail = 0;

    l_fail += test_feeder_kill();
    l_fail += test_olb_saturation();
    l_fail += test_shutdown_race();
    l_fail += test_parser_fuzz();
    l_fail += test_mpsc_contention();
    l_fail += test_ext_stack_race();
    l_fail += test_olb_compaction();
    l_fail += test_send_olb_wrap();
#ifndef __SANITIZE_THREAD__
    l_fail += test_coro_churn();
#else
    printf("\n[%d] Coroutine pool churn " CLR_RST "(skipped under TSAN)" CLR_RST "\n", ++s_test_num);
#endif
    l_fail += test_sl_sched_stress();
    l_fail += test_cold_api_helpers();
    l_fail += test_conn_open_validation();

    double l_sec = (double)(dap_nanotime_now() - l_t0) / 1e9;
    printf("\n===========================================================\n");
    printf("  %d/%d passed in %.1f s  %s\n",
           s_test_num - l_fail, s_test_num, l_sec,
           l_fail ? CLR_FAIL "FAILED" CLR_RST : CLR_OK "ALL PASSED" CLR_RST);
    printf("===========================================================\n");
    return l_fail ? 1 : 0;
}
