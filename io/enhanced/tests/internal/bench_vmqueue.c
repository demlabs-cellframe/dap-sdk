/*
 * bench_vmqueue.c — Benchmark: SPSC (gen-reset), MPSC fan-in, mutex baseline.
 *
 * Adapted for enhanced module API.
 */
#include "dap_vmqueue.h"
#include "dap_io_plat.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#define DEFAULT_MSGS       10000000
#define RESERVE_BYTES      Mbytes(512)
#define SMALL_RESERVE      Mbytes(1)
#define MPSC_PRODUCERS     4
#define PAYLOAD_SIZE       sizeof(uint64_t)

static inline uint64_t dap_nanotime_now(void)
{
    struct timespec l_ts;
    clock_gettime(CLOCK_MONOTONIC, &l_ts);
    return (uint64_t)l_ts.tv_sec * 1000000000ULL + (uint64_t)l_ts.tv_nsec;
}

/* ------------------------------------------------------------------ */
/*  SPSC benchmark                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    dap_vmqueue_t *q;
    size_t         n;
    uint64_t       checksum;
    int            use_wait;
} spsc_ctx_t;

static void *s_spsc_producer(void *a_arg)
{
    spsc_ctx_t *l_ctx = (spsc_ctx_t *)a_arg;
    dap_vmqueue_t *l_q = l_ctx->q;
    if (l_ctx->use_wait) {
        for (size_t i = 0; i < l_ctx->n; ++i) {
            uint64_t l_val = i;
            l_ctx->checksum += l_val;
            dap_vmqueue_push_wait(l_q, 1, 0, &l_val, sizeof(l_val));
        }
    } else {
        for (size_t i = 0; i < l_ctx->n; ++i) {
            uint64_t l_val = i;
            l_ctx->checksum += l_val;
            while (!dap_vmqueue_push(l_q, 1, 0, &l_val, sizeof(l_val)))
                dap_cpu_relax();
        }
    }
    return NULL;
}

static void s_sum_read(const void *a_payload, uint32_t a_len,
                       uint8_t a_type, uint8_t a_pri, void *a_arg)
{
    (void)a_len; (void)a_type; (void)a_pri;
    *(uint64_t *)a_arg += *(const uint64_t *)a_payload;
}

static void bench_spsc(size_t a_n, size_t a_reserve, int a_use_wait)
{
    dap_vmqueue_t *l_q = dap_vmqueue_create(a_reserve);
    if (!l_q) { fprintf(stderr, "SPSC: mmap failed\n"); return; }

    spsc_ctx_t l_ctx = { .q = l_q, .n = a_n, .checksum = 0, .use_wait = a_use_wait };
    pthread_t l_prod;
    pthread_create(&l_prod, NULL, s_spsc_producer, &l_ctx);

    uint64_t l_consumer_sum = 0, l_total = 0;
    uint64_t l_t0 = dap_nanotime_now();

    while (l_total < a_n) {
        size_t l_got = dap_vmqueue_drain_typed(l_q, s_sum_read, &l_consumer_sum);
        if (!l_got) dap_cpu_relax();
        l_total += l_got;
    }
    uint64_t l_elapsed = dap_nanotime_now() - l_t0;
    pthread_join(l_prod, NULL);

    double l_mops = (double)a_n / ((double)l_elapsed / 1e9) / 1e6;
    double l_ns   = (double)l_elapsed / (double)a_n;
    uint32_t l_resets = DAP_VMQ_GEN(atomic_load(&l_q->tail_gen));
    printf("SPSC  1->1  %10zu msgs %4zuMB %s: %7.2f Mops/s  %6.1f ns/msg  resets %-6u checksum %s\n",
           a_n, a_reserve >> 20, a_use_wait ? "wait" : "spin",
           l_mops, l_ns, l_resets,
           l_ctx.checksum == l_consumer_sum ? "OK" : "MISMATCH");
    dap_vmqueue_destroy(l_q);
}

/* ------------------------------------------------------------------ */
/*  MPSC benchmark (per-lane fan-in)                                   */
/* ------------------------------------------------------------------ */

typedef struct {
    dap_vmqueue_mpsc_t *q;
    unsigned             lane;
    size_t               start;
    size_t               count;
    uint64_t             checksum;
} mpsc_prod_ctx_t;

static void *s_mpsc_producer(void *a_arg)
{
    mpsc_prod_ctx_t *l_ctx = (mpsc_prod_ctx_t *)a_arg;
    for (size_t i = 0; i < l_ctx->count; ++i) {
        uint64_t l_val = l_ctx->start + i;
        l_ctx->checksum += l_val;
        while (!dap_vmqueue_mpsc_push(l_ctx->q, l_ctx->lane, 1, 0, &l_val, sizeof(l_val)))
            dap_cpu_relax();
    }
    return NULL;
}


static void bench_mpsc(size_t a_n, int a_producers, size_t a_lane_reserve)
{
    size_t l_caps[MPSC_PRODUCERS];
    for (int i = 0; i < a_producers; ++i)
        l_caps[i] = a_lane_reserve;
    dap_vmqueue_mpsc_t *l_q = dap_vmqueue_mpsc_create((unsigned)a_producers, l_caps);
    if (!l_q) { fprintf(stderr, "MPSC: mmap failed\n"); return; }

    size_t l_per = a_n / (size_t)a_producers;
    mpsc_prod_ctx_t l_ctxs[MPSC_PRODUCERS];
    pthread_t l_threads[MPSC_PRODUCERS];
    uint64_t l_prod_sum = 0;

    uint64_t l_t0 = dap_nanotime_now();
    for (int i = 0; i < a_producers; ++i) {
        l_ctxs[i] = (mpsc_prod_ctx_t){
            .q = l_q, .lane = (unsigned)i,
            .start = (size_t)i * l_per,
            .count = (i == a_producers - 1) ? a_n - (size_t)i * l_per : l_per,
            .checksum = 0
        };
        pthread_create(&l_threads[i], NULL, s_mpsc_producer, &l_ctxs[i]);
    }

    uint64_t l_consumer_sum = 0, l_total = 0;
    while (l_total < a_n) {
        size_t l_got = dap_vmqueue_mpsc_drain_typed(l_q, s_sum_read, &l_consumer_sum);
        if (!l_got) dap_cpu_relax();
        l_total += l_got;
    }
    uint64_t l_elapsed = dap_nanotime_now() - l_t0;

    for (int i = 0; i < a_producers; ++i) {
        pthread_join(l_threads[i], NULL);
        l_prod_sum += l_ctxs[i].checksum;
    }

    double l_mops = (double)a_n / ((double)l_elapsed / 1e9) / 1e6;
    double l_ns   = (double)l_elapsed / (double)a_n;
    printf("MPSC  %d->1  %10zu msgs %4zuMB: %7.2f Mops/s  %6.1f ns/msg  checksum %s\n",
           a_producers, a_n, (a_lane_reserve * (size_t)a_producers) >> 20,
           l_mops, l_ns,
           l_prod_sum == l_consumer_sum ? "OK" : "MISMATCH");
    dap_vmqueue_mpsc_destroy(l_q);
}

/* ------------------------------------------------------------------ */
/*  Mutex baseline                                                     */
/* ------------------------------------------------------------------ */

typedef struct sq_node {
    struct sq_node *next;
    uint64_t        val;
} sq_node_t;

typedef struct {
    sq_node_t      *head;
    sq_node_t      *tail;
    pthread_mutex_t mtx;
} simple_queue_t;

static inline void sq_init(simple_queue_t *a_q)
{
    a_q->head = a_q->tail = NULL;
    pthread_mutex_init(&a_q->mtx, NULL);
}

static inline void sq_push(simple_queue_t *a_q, uint64_t a_val)
{
    sq_node_t *l_n = (sq_node_t *)malloc(sizeof(sq_node_t));
    l_n->val  = a_val;
    l_n->next = NULL;
    pthread_mutex_lock(&a_q->mtx);
    if (a_q->tail) a_q->tail->next = l_n; else a_q->head = l_n;
    a_q->tail = l_n;
    pthread_mutex_unlock(&a_q->mtx);
}

static inline int sq_pop(simple_queue_t *a_q, uint64_t *a_out)
{
    pthread_mutex_lock(&a_q->mtx);
    sq_node_t *l_n = a_q->head;
    if (l_n) {
        a_q->head = l_n->next;
        if (!a_q->head) a_q->tail = NULL;
    }
    pthread_mutex_unlock(&a_q->mtx);
    if (!l_n) return 0;
    *a_out = l_n->val;
    free(l_n);
    return 1;
}

static inline void sq_destroy(simple_queue_t *a_q)
{
    uint64_t l_tmp;
    while (sq_pop(a_q, &l_tmp));
    pthread_mutex_destroy(&a_q->mtx);
}

typedef struct {
    simple_queue_t *q;
    size_t          start;
    size_t          count;
    uint64_t        checksum;
} sq_prod_ctx_t;

static void *s_sq_producer(void *a_arg)
{
    sq_prod_ctx_t *l_ctx = (sq_prod_ctx_t *)a_arg;
    for (size_t i = 0; i < l_ctx->count; ++i) {
        uint64_t l_val = l_ctx->start + i;
        l_ctx->checksum += l_val;
        sq_push(l_ctx->q, l_val);
    }
    return NULL;
}

static void bench_mutex(size_t a_n, int a_producers)
{
    simple_queue_t l_q;
    sq_init(&l_q);

    sq_prod_ctx_t l_ctxs[MPSC_PRODUCERS];
    pthread_t l_threads[MPSC_PRODUCERS];
    size_t l_per = a_n / (size_t)a_producers;
    uint64_t l_prod_sum = 0;

    uint64_t l_t0 = dap_nanotime_now();
    for (int i = 0; i < a_producers; ++i) {
        l_ctxs[i] = (sq_prod_ctx_t){
            .q = &l_q, .start = (size_t)i * l_per,
            .count = (i == a_producers - 1) ? a_n - (size_t)i * l_per : l_per,
            .checksum = 0
        };
        pthread_create(&l_threads[i], NULL, s_sq_producer, &l_ctxs[i]);
    }

    uint64_t l_consumer_sum = 0, l_total = 0, l_val;
    while (l_total < a_n) {
        if (sq_pop(&l_q, &l_val)) {
            l_consumer_sum += l_val;
            ++l_total;
        } else {
            dap_cpu_relax();
        }
    }
    uint64_t l_elapsed = dap_nanotime_now() - l_t0;

    for (int i = 0; i < a_producers; ++i) {
        pthread_join(l_threads[i], NULL);
        l_prod_sum += l_ctxs[i].checksum;
    }

    double l_mops = (double)a_n / ((double)l_elapsed / 1e9) / 1e6;
    double l_ns   = (double)l_elapsed / (double)a_n;
    printf("MUTEX %d->1  %10zu msgs      : %7.2f Mops/s  %6.1f ns/msg  checksum %s\n",
           a_producers, a_n, l_mops, l_ns,
           l_prod_sum == l_consumer_sum ? "OK" : "MISMATCH");
    sq_destroy(&l_q);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    size_t l_n = argc > 1 ? (size_t)atol(argv[1]) : DEFAULT_MSGS;

    printf("=== vmqueue benchmark (enhanced)  N=%zu  reserve=%zuMB  producers=%d ===\n\n",
           l_n, RESERVE_BYTES >> 20, MPSC_PRODUCERS);

    bench_spsc(l_n, RESERVE_BYTES, 0);
    bench_spsc(l_n, RESERVE_BYTES, 1);
    bench_spsc(l_n, SMALL_RESERVE, 0);
    bench_spsc(l_n, SMALL_RESERVE, 1);
    printf("\n");
    bench_mpsc(l_n, 1, RESERVE_BYTES);
    bench_mpsc(l_n, MPSC_PRODUCERS, RESERVE_BYTES / MPSC_PRODUCERS);
    bench_mpsc(l_n, MPSC_PRODUCERS, SMALL_RESERVE);
    printf("\n");
    bench_mutex(l_n, 1);
    bench_mutex(l_n, MPSC_PRODUCERS);
    return 0;
}
