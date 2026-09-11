#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include "dap_common.h"
#include "dap_test.h"
#include "dap_thread_pool.h"

/* Compile the production implementation with thread-local allocation injection. */
static _Thread_local bool s_fail_alloc;
static void *s_calloc(size_t a_count, size_t a_size)
{
    if (s_fail_alloc) {
        s_fail_alloc = false;
        return NULL;
    }
    return calloc(a_count, a_size);
}
#undef DAP_NEW_Z
#define DAP_NEW_Z(t) ((t *)s_calloc(1, sizeof(t)))
#include "../../../module/io/flow/dap_thread_pool.c"

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool entered, release;
} gate_t;

typedef struct {
    unsigned executed, freed, callbacks;
    dap_thread_pool_t *pool;
    bool reenter;
} counts_t;

typedef struct {
    counts_t *counts;
} arg_t;

static void *s_arg(counts_t *a_counts)
{
    arg_t *l_arg = malloc(sizeof(*l_arg));
    dap_assert(l_arg != NULL, "argument allocated");
    l_arg->counts = a_counts;
    return l_arg;
}

static void *s_run(void *a_arg)
{
    arg_t *l_arg = a_arg;
    ++l_arg->counts->executed;
    free(l_arg);
    return NULL;
}

static void s_free(void *a_arg)
{
    arg_t *l_arg = a_arg;
    counts_t *l_counts = l_arg->counts;
    ++l_counts->freed;
    if (l_counts->reenter) {
        /* This query takes the worker mutex: rejection cleanup must not hold it. */
        (void)dap_thread_pool_get_pending_count(l_counts->pool);
        dap_assert(dap_thread_pool_submit_to(l_counts->pool, 0, s_run, NULL,
                                            NULL, NULL) < 0, "reentrant submit rejected");
    }
    free(l_arg);
}

static void s_callback(dap_thread_pool_t *a_pool, dap_thread_t a_thread,
                       void *a_result, void *a_arg)
{
    (void)a_pool;
    (void)a_thread;
    (void)a_result;
    ++((counts_t *)a_arg)->callbacks;
}

static int s_owned(dap_thread_pool_t *a_pool, counts_t *a_counts)
{
    return dap_thread_pool_submit_to_owned(a_pool, 0, s_run, s_arg(a_counts),
                                           s_free, NULL, NULL);
}

static void *s_block(void *a_arg)
{
    gate_t *l_gate = a_arg;
    pthread_mutex_lock(&l_gate->mutex);
    l_gate->entered = true;
    pthread_cond_broadcast(&l_gate->cond);
    while (!l_gate->release)
        pthread_cond_wait(&l_gate->cond, &l_gate->mutex);
    pthread_mutex_unlock(&l_gate->mutex);
    return NULL;
}

static void s_validation(void)
{
    dap_thread_pool_t *l_pool = dap_thread_pool_create(1, 1);
    dap_assert(l_pool != NULL, "pool created");
    counts_t l_counts = {0};
    dap_assert(s_owned(NULL, &l_counts) == -1, "NULL pool rejected");
    dap_assert(dap_thread_pool_submit_to_owned(l_pool, 1, s_run, s_arg(&l_counts),
               s_free, NULL, NULL) == -1, "boundary index rejected");
    dap_assert(dap_thread_pool_submit_to_owned(l_pool, UINT32_MAX, s_run, s_arg(&l_counts),
               s_free, NULL, NULL) == -1, "large index rejected");
    dap_assert(dap_thread_pool_submit_to_owned(l_pool, 0, NULL, s_arg(&l_counts),
               s_free, NULL, NULL) == -1, "NULL task rejected");
    dap_assert(l_counts.freed == 4 && !l_counts.executed, "invalid args consumed once");
    void *l_arg = s_arg(&l_counts);
    dap_assert(dap_thread_pool_submit_to_owned(l_pool, 0, s_run, l_arg,
               NULL, NULL, NULL) == -1, "NULL destructor rejected");
    dap_assert(l_counts.freed == 4, "NULL destructor retains caller ownership");
    free(l_arg);
    s_fail_alloc = true;
    dap_assert(s_owned(l_pool, &l_counts) == -4, "owned allocation failure");
    dap_assert(!s_fail_alloc && l_counts.freed == 5, "OOM consumes argument once");
    l_arg = s_arg(&l_counts);
    s_fail_alloc = true;
    dap_assert(dap_thread_pool_submit_to(l_pool, 0, s_run, l_arg, NULL, NULL) == -4,
               "legacy allocation failure");
    dap_assert(dap_thread_pool_submit(NULL, s_run, l_arg, NULL, NULL) == -1,
               "legacy invalid pool");
    dap_assert(dap_thread_pool_submit_to(l_pool, 1, s_run, l_arg, NULL, NULL) == -1,
               "legacy invalid index");
    dap_assert(l_counts.freed == 5, "legacy retains argument");
    free(l_arg);
    dap_thread_pool_delete(l_pool);
}

static void s_queue_and_shutdown(void)
{
    dap_thread_pool_t *l_pool = dap_thread_pool_create(1, 1);
    dap_assert(l_pool != NULL, "pool created");
    gate_t l_gate = { .mutex = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER };
    counts_t l_run = {0}, l_reject = { .pool = l_pool, .reenter = true };
    dap_assert(dap_thread_pool_submit_to(l_pool, 0, s_block, &l_gate, NULL, NULL) == 0,
               "blocking task submitted");
    pthread_mutex_lock(&l_gate.mutex);
    while (!l_gate.entered)
        pthread_cond_wait(&l_gate.cond, &l_gate.mutex);
    pthread_mutex_unlock(&l_gate.mutex);
    dap_assert(dap_thread_pool_submit_to_owned(l_pool, 0, s_run, s_arg(&l_run), s_free,
               s_callback, &l_run) == 0, "owned task queued");
    dap_assert(s_owned(l_pool, &l_reject) == -3, "full queue rejection permits reentry");
    void *l_arg = s_arg(&l_reject);
    dap_assert(dap_thread_pool_submit(l_pool, s_run, l_arg, NULL, NULL) == -3,
               "legacy full queue rejection");
    free(l_arg);
    pthread_mutex_lock(&l_gate.mutex);
    l_gate.release = true;
    pthread_cond_broadcast(&l_gate.cond);
    pthread_mutex_unlock(&l_gate.mutex);
    dap_assert(dap_thread_pool_shutdown(l_pool, 0) == 0, "shutdown drains queue");
    dap_assert(s_owned(l_pool, &l_reject) == -2, "shutdown rejection permits reentry");
    dap_assert(dap_thread_pool_shutdown(l_pool, 1) == 0, "repeated shutdown safe");
    dap_thread_pool_delete(l_pool);
    dap_assert(l_run.executed == 1 && l_run.callbacks == 1 && !l_run.freed,
               "executed arg belongs to task, not pool");
    dap_assert(l_reject.freed == 2 && !l_reject.executed, "rejections consumed exactly once");
    pthread_cond_destroy(&l_gate.cond);
    pthread_mutex_destroy(&l_gate.mutex);
}

static void s_delete_drains(void)
{
    dap_thread_pool_t *l_pool = dap_thread_pool_create(1, 0);
    dap_assert(l_pool != NULL, "pool created");
    counts_t l_counts = {0};
    for (unsigned i = 0; i < 32; ++i)
        dap_assert(s_owned(l_pool, &l_counts) == 0, "NULL completion callback accepted");
    dap_thread_pool_delete(l_pool);
    dap_assert(l_counts.executed == 32 && !l_counts.freed, "delete joins and drains owned tasks");
    dap_thread_pool_delete(NULL);
}

int main(void)
{
    s_validation();
    s_queue_and_shutdown();
    s_delete_drains();
    return 0;
}
