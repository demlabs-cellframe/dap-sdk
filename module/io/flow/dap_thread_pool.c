/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * CellFrame       https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 */

#include <pthread.h>
#include <errno.h>
#ifndef DAP_OS_WINDOWS
#include <unistd.h>
#endif
#include <stdatomic.h>
#include "dap_thread_pool.h"
#include "dap_thread.h"
#include "dap_events.h"
#include "dap_common.h"

#define LOG_TAG "dap_thread_pool"

/**
 * @brief Task structure (per-thread queue node)
 */
typedef struct dap_thread_pool_task {
    dap_thread_pool_task_func_t func;
    void *arg;
    dap_thread_pool_callback_t callback;
    void *callback_arg;
    struct dap_thread_pool_task *next;
} dap_thread_pool_task_t;

/**
 * @brief Per-thread worker context
 */
typedef struct dap_thread_pool_worker {
    pthread_t thread;
    uint32_t id;

    // Per-thread task queue
    dap_thread_pool_task_t *queue_head;
    dap_thread_pool_task_t *queue_tail;
    uint32_t queue_count;
    uint32_t active_tasks;

    // Per-thread synchronization
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool shutdown;

    // Back-pointer to owning pool
    struct dap_thread_pool *pool;
} dap_thread_pool_worker_t;

/**
 * @brief Thread pool structure (per-thread queues, supports sticky binding)
 */
struct dap_thread_pool {
    dap_thread_pool_worker_t *workers;
    uint32_t num_threads;
    uint32_t queue_size;          // per-thread limit (0 = unlimited)
    _Atomic uint32_t next_thread; // round-robin counter for submit()
    bool threads_joined;          // guard against double pthread_join
};

/**
 * @brief Worker thread function (each thread services its own queue)
 */
static void *s_worker_thread(void *a_arg)
{
    dap_thread_pool_worker_t *l_worker = (dap_thread_pool_worker_t *)a_arg;
    dap_thread_pool_t *l_pool = l_worker->pool;
    dap_thread_t l_self = dap_thread_self();

    while (true) {
        pthread_mutex_lock(&l_worker->mutex);

        // Wait for task or shutdown
        while (l_worker->queue_count == 0 && !l_worker->shutdown)
            pthread_cond_wait(&l_worker->cond, &l_worker->mutex);

        // Check shutdown with empty queue
        if (l_worker->shutdown && l_worker->queue_count == 0) {
            pthread_mutex_unlock(&l_worker->mutex);
            break;
        }

        // Dequeue task
        dap_thread_pool_task_t *l_task = l_worker->queue_head;
        if (l_task) {
            l_worker->queue_head = l_task->next;
            if (!l_worker->queue_head)
                l_worker->queue_tail = NULL;
            l_worker->queue_count--;
            l_worker->active_tasks++;
        }

        pthread_mutex_unlock(&l_worker->mutex);

        // Execute task outside lock
        if (l_task) {
            void *l_result = NULL;
            if (l_task->func)
                l_result = l_task->func(l_task->arg);

            if (l_task->callback)
                l_task->callback(l_pool, l_self, l_result, l_task->callback_arg);

            DAP_DELETE(l_task);

            pthread_mutex_lock(&l_worker->mutex);
            l_worker->active_tasks--;
            pthread_cond_broadcast(&l_worker->cond);
            pthread_mutex_unlock(&l_worker->mutex);
        }
    }

    return NULL;
}

dap_thread_pool_t *dap_thread_pool_create(uint32_t a_num_threads, uint32_t a_queue_size)
{
    if (a_num_threads == 0) {
        uint32_t l_ncpus = dap_get_cpu_count();
        a_num_threads = l_ncpus > 0 ? l_ncpus : 4;
    }

    dap_thread_pool_t *l_pool = DAP_NEW_Z(dap_thread_pool_t);
    if (!l_pool) {
        log_it(L_CRITICAL, "Failed to allocate thread pool");
        return NULL;
    }

    l_pool->num_threads = a_num_threads;
    l_pool->queue_size = a_queue_size;
    l_pool->threads_joined = false;
    atomic_store(&l_pool->next_thread, 0);

    l_pool->workers = DAP_NEW_Z_COUNT(dap_thread_pool_worker_t, a_num_threads);
    if (!l_pool->workers) {
        log_it(L_CRITICAL, "Failed to allocate worker array");
        DAP_DELETE(l_pool);
        return NULL;
    }

    for (uint32_t i = 0; i < a_num_threads; i++) {
        dap_thread_pool_worker_t *l_w = &l_pool->workers[i];
        l_w->id = i;
        l_w->pool = l_pool;
        l_w->shutdown = false;
        pthread_mutex_init(&l_w->mutex, NULL);
        pthread_cond_init(&l_w->cond, NULL);
    }

    {
    uint32_t l_ncpus = dap_get_cpu_count();

    for (uint32_t i = 0; i < a_num_threads; i++) {
        dap_thread_pool_worker_t *l_w = &l_pool->workers[i];
        int l_ret = pthread_create(&l_w->thread, NULL, s_worker_thread, l_w);
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to create worker thread %u: %s", i, strerror(l_ret));
            for (uint32_t j = 0; j < i; j++) {
                pthread_mutex_lock(&l_pool->workers[j].mutex);
                l_pool->workers[j].shutdown = true;
                pthread_cond_signal(&l_pool->workers[j].cond);
                pthread_mutex_unlock(&l_pool->workers[j].mutex);
            }
            for (uint32_t j = 0; j < i; j++) {
                pthread_join(l_pool->workers[j].thread, NULL);
                pthread_mutex_destroy(&l_pool->workers[j].mutex);
                pthread_cond_destroy(&l_pool->workers[j].cond);
            }
            for (uint32_t j = i; j < a_num_threads; j++) {
                pthread_mutex_destroy(&l_pool->workers[j].mutex);
                pthread_cond_destroy(&l_pool->workers[j].cond);
            }
            DAP_DELETE(l_pool->workers);
            DAP_DELETE(l_pool);
            return NULL;
        }

        char l_name[32];
        snprintf(l_name, sizeof(l_name), "tp_worker_%u", i);
        dap_thread_set_name(l_w->thread, l_name);

        if (l_ncpus > 0) {
            uint32_t l_cpu_id = i % l_ncpus;
            int l_aff_ret = dap_thread_set_affinity(l_w->thread, l_cpu_id);
            if (l_aff_ret == 0)
                log_it(L_DEBUG, "Worker %u bound to CPU core %u", i, l_cpu_id);
        }
    }

    log_it(L_INFO, "Created thread pool with %u workers (per-thread queues)", a_num_threads);
    }
    return l_pool;
}

/**
 * @brief Internal: enqueue task to a specific worker
 *
 * Allocates task BEFORE taking the lock to minimize lock hold time.
 */
static int s_submit_to_worker(dap_thread_pool_worker_t *a_worker, uint32_t a_queue_size,
                               dap_thread_pool_task_func_t a_task_func, void *a_task_arg,
                               dap_thread_pool_callback_t a_callback, void *a_callback_arg)
{
    dap_thread_pool_task_t *l_task = DAP_NEW_Z(dap_thread_pool_task_t);
    if (!l_task)
        return -4;

    l_task->func = a_task_func;
    l_task->arg = a_task_arg;
    l_task->callback = a_callback;
    l_task->callback_arg = a_callback_arg;
    l_task->next = NULL;

    pthread_mutex_lock(&a_worker->mutex);

    if (a_worker->shutdown) {
        pthread_mutex_unlock(&a_worker->mutex);
        DAP_DELETE(l_task);
        return -2;
    }

    if (a_queue_size > 0 && a_worker->queue_count >= a_queue_size) {
        pthread_mutex_unlock(&a_worker->mutex);
        DAP_DELETE(l_task);
        return -3;
    }

    if (a_worker->queue_tail)
        a_worker->queue_tail->next = l_task;
    else
        a_worker->queue_head = l_task;
    a_worker->queue_tail = l_task;
    a_worker->queue_count++;

    pthread_cond_signal(&a_worker->cond);
    pthread_mutex_unlock(&a_worker->mutex);

    return 0;
}

int dap_thread_pool_submit(dap_thread_pool_t *a_pool,
                           dap_thread_pool_task_func_t a_task_func,
                           void *a_task_arg,
                           dap_thread_pool_callback_t a_callback,
                           void *a_callback_arg)
{
    if (!a_pool || !a_task_func)
        return -1;

    // Round-robin selection
    uint32_t l_idx = atomic_fetch_add(&a_pool->next_thread, 1) % a_pool->num_threads;
    return s_submit_to_worker(&a_pool->workers[l_idx], a_pool->queue_size,
                               a_task_func, a_task_arg, a_callback, a_callback_arg);
}

int dap_thread_pool_submit_to(dap_thread_pool_t *a_pool,
                              uint32_t a_thread_idx,
                              dap_thread_pool_task_func_t a_task_func,
                              void *a_task_arg,
                              dap_thread_pool_callback_t a_callback,
                              void *a_callback_arg)
{
    if (!a_pool || !a_task_func)
        return -1;
    if (a_thread_idx >= a_pool->num_threads) {
        log_it(L_ERROR, "Thread index %u out of range (pool has %u threads)",
               a_thread_idx, a_pool->num_threads);
        return -1;
    }

    return s_submit_to_worker(&a_pool->workers[a_thread_idx], a_pool->queue_size,
                               a_task_func, a_task_arg, a_callback, a_callback_arg);
}

uint32_t dap_thread_pool_get_thread_count(dap_thread_pool_t *a_pool)
{
    return a_pool ? a_pool->num_threads : 0;
}

uint32_t dap_thread_pool_get_pending_count(dap_thread_pool_t *a_pool)
{
    if (!a_pool)
        return 0;

    uint32_t l_total = 0;
    for (uint32_t i = 0; i < a_pool->num_threads; i++) {
        dap_thread_pool_worker_t *l_w = &a_pool->workers[i];
        pthread_mutex_lock(&l_w->mutex);
        l_total += l_w->queue_count + l_w->active_tasks;
        pthread_mutex_unlock(&l_w->mutex);
    }
    return l_total;
}

int dap_thread_pool_shutdown(dap_thread_pool_t *a_pool, uint32_t a_timeout_ms)
{
    if (!a_pool || !a_pool->workers)
        return -1;

    if (a_pool->threads_joined)
        return 0;

    for (uint32_t i = 0; i < a_pool->num_threads; i++) {
        dap_thread_pool_worker_t *l_w = &a_pool->workers[i];
        pthread_mutex_lock(&l_w->mutex);
        l_w->shutdown = true;
        pthread_cond_broadcast(&l_w->cond);
        pthread_mutex_unlock(&l_w->mutex);
    }

    int l_rc = 0;

    if (a_timeout_ms > 0) {
        struct timespec l_deadline;
        clock_gettime(CLOCK_REALTIME, &l_deadline);
        l_deadline.tv_sec += a_timeout_ms / 1000;
        l_deadline.tv_nsec += (a_timeout_ms % 1000) * 1000000;
        if (l_deadline.tv_nsec >= 1000000000) {
            l_deadline.tv_sec++;
            l_deadline.tv_nsec -= 1000000000;
        }

        for (uint32_t i = 0; i < a_pool->num_threads; i++) {
            dap_thread_pool_worker_t *l_w = &a_pool->workers[i];
            pthread_mutex_lock(&l_w->mutex);
            while (l_w->queue_count > 0 || l_w->active_tasks > 0) {
                int l_ret = pthread_cond_timedwait(&l_w->cond, &l_w->mutex, &l_deadline);
                if (l_ret == ETIMEDOUT) {
                    log_it(L_WARNING, "Thread pool shutdown timeout: worker %u has %u queued, %u active",
                           i, l_w->queue_count, l_w->active_tasks);
                    l_rc = -2;
                    pthread_mutex_unlock(&l_w->mutex);
                    goto join_threads;
                }
            }
            pthread_mutex_unlock(&l_w->mutex);
        }
    }

join_threads:
    for (uint32_t i = 0; i < a_pool->num_threads; i++)
        pthread_join(a_pool->workers[i].thread, NULL);
    a_pool->threads_joined = true;

    log_it(L_INFO, "Thread pool shut down (%u workers)", a_pool->num_threads);
    return l_rc;
}

void dap_thread_pool_delete(dap_thread_pool_t *a_pool)
{
    if (!a_pool)
        return;

    // Shutdown if not already done
    if (!a_pool->threads_joined)
        dap_thread_pool_shutdown(a_pool, 5000);

    // Free remaining tasks in all worker queues
    if (a_pool->workers) {
        for (uint32_t i = 0; i < a_pool->num_threads; i++) {
            dap_thread_pool_worker_t *l_w = &a_pool->workers[i];
            dap_thread_pool_task_t *l_task = l_w->queue_head;
            while (l_task) {
                dap_thread_pool_task_t *l_next = l_task->next;
                DAP_DELETE(l_task);
                l_task = l_next;
            }
            pthread_mutex_destroy(&l_w->mutex);
            pthread_cond_destroy(&l_w->cond);
        }
        DAP_DELETE(a_pool->workers);
    }

    DAP_DELETE(a_pool);
}
