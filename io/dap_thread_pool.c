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
#include <unistd.h>
#include "dap_thread_pool.h"
#include "dap_thread.h"
#include "dap_common.h"

#define LOG_TAG "dap_thread_pool"

/**
 * @brief Task structure
 */
typedef struct dap_thread_pool_task {
    dap_thread_pool_task_func_t func;
    void *arg;
    dap_thread_pool_callback_t callback;
    void *callback_arg;
    struct dap_thread_pool_task *next;
} dap_thread_pool_task_t;

/**
 * @brief Thread pool structure
 */
struct dap_thread_pool {
    pthread_t *threads;
    uint32_t num_threads;
    uint32_t queue_size;
    
    // Task queue
    dap_thread_pool_task_t *queue_head;
    dap_thread_pool_task_t *queue_tail;
    uint32_t queue_count;
    
    // Synchronization
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    
    // State
    bool shutdown;
    uint32_t active_tasks;
};

/**
 * @brief Worker thread function
 */
static void* s_worker_thread(void *a_arg)
{
    dap_thread_pool_t *l_pool = (dap_thread_pool_t *)a_arg;
    dap_thread_t l_self = dap_thread_self();  // Get worker thread handle once
    
    while (true) {
        pthread_mutex_lock(&l_pool->mutex);
        
        // Wait for task or shutdown
        while (l_pool->queue_count == 0 && !l_pool->shutdown) {
            pthread_cond_wait(&l_pool->cond, &l_pool->mutex);
        }
        
        // Check shutdown
        if (l_pool->shutdown && l_pool->queue_count == 0) {
            pthread_mutex_unlock(&l_pool->mutex);
            break;
        }
        
        // Get task from queue
        dap_thread_pool_task_t *l_task = l_pool->queue_head;
        if (l_task) {
            l_pool->queue_head = l_task->next;
            if (!l_pool->queue_head) {
                l_pool->queue_tail = NULL;
            }
            l_pool->queue_count--;
            l_pool->active_tasks++;
        }
        
        pthread_mutex_unlock(&l_pool->mutex);
        
        // Execute task
        if (l_task) {
            void *l_result = NULL;
            if (l_task->func) {
                l_result = l_task->func(l_task->arg);
            }
            
            // Call completion callback with pool, worker thread, result, and user arg
            if (l_task->callback) {
                l_task->callback(l_pool, l_self, l_result, l_task->callback_arg);
            }
            
            DAP_DELETE(l_task);
            
            // Decrement active tasks
            pthread_mutex_lock(&l_pool->mutex);
            l_pool->active_tasks--;
            pthread_cond_broadcast(&l_pool->cond);
            pthread_mutex_unlock(&l_pool->mutex);
        }
    }
    
    return NULL;
}

dap_thread_pool_t* dap_thread_pool_create(uint32_t a_num_threads, uint32_t a_queue_size)
{
    // Auto-detect CPU count
    if (a_num_threads == 0) {
        long l_ncpus = sysconf(_SC_NPROCESSORS_ONLN);
        a_num_threads = (l_ncpus > 0) ? (uint32_t)l_ncpus : 4;
    }
    
    dap_thread_pool_t *l_pool = DAP_NEW_Z(dap_thread_pool_t);
    if (!l_pool) {
        log_it(L_CRITICAL, "Failed to allocate thread pool");
        return NULL;
    }
    
    l_pool->num_threads = a_num_threads;
    l_pool->queue_size = a_queue_size;
    l_pool->shutdown = false;
    l_pool->active_tasks = 0;
    
    pthread_mutex_init(&l_pool->mutex, NULL);
    pthread_cond_init(&l_pool->cond, NULL);
    
    // Create worker threads
    l_pool->threads = DAP_NEW_Z_COUNT(pthread_t, a_num_threads);
    if (!l_pool->threads) {
        log_it(L_CRITICAL, "Failed to allocate thread array");
        pthread_mutex_destroy(&l_pool->mutex);
        pthread_cond_destroy(&l_pool->cond);
        DAP_DELETE(l_pool);
        return NULL;
    }
    
    for (uint32_t i = 0; i < a_num_threads; i++) {
        int l_ret = pthread_create(&l_pool->threads[i], NULL, s_worker_thread, l_pool);
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to create worker thread %u: %s", i, strerror(l_ret));
            // Cleanup partial initialization
            l_pool->num_threads = i;
            dap_thread_pool_delete(l_pool);
            return NULL;
        }
        
        // Set thread name for debugging
        char l_name[32];  // Increased to fit "kem_worker_" + 10 digits + null terminator
        snprintf(l_name, sizeof(l_name), "kem_worker_%u", i);
        dap_thread_set_name(l_pool->threads[i], l_name);
        
        // Bind thread to CPU core (i % num_cpus for wrap-around)
        long l_ncpus = sysconf(_SC_NPROCESSORS_ONLN);
        if (l_ncpus > 0) {
            uint32_t l_cpu_id = i % (uint32_t)l_ncpus;
            int l_aff_ret = dap_thread_set_affinity(l_pool->threads[i], l_cpu_id);
            if (l_aff_ret == 0) {
                log_it(L_INFO, "KEM worker %u bound to CPU core %u", i, l_cpu_id);
            }
        }
    }
    
    log_it(L_INFO, "Created thread pool with %u workers", a_num_threads);
    
    return l_pool;
}

int dap_thread_pool_submit(dap_thread_pool_t *a_pool,
                           dap_thread_pool_task_func_t a_task_func,
                           void *a_task_arg,
                           dap_thread_pool_callback_t a_callback,
                           void *a_callback_arg)
{
    if (!a_pool || !a_task_func) {
        return -1;
    }
    
    pthread_mutex_lock(&a_pool->mutex);
    
    // Check shutdown
    if (a_pool->shutdown) {
        pthread_mutex_unlock(&a_pool->mutex);
        return -2;
    }
    
    // Check queue size limit
    if (a_pool->queue_size > 0 && a_pool->queue_count >= a_pool->queue_size) {
        pthread_mutex_unlock(&a_pool->mutex);
        log_it(L_WARNING, "Thread pool queue full (%u tasks)", a_pool->queue_count);
        return -3;
    }
    
    // Create task
    dap_thread_pool_task_t *l_task = DAP_NEW_Z(dap_thread_pool_task_t);
    if (!l_task) {
        pthread_mutex_unlock(&a_pool->mutex);
        return -4;
    }
    
    l_task->func = a_task_func;
    l_task->arg = a_task_arg;
    l_task->callback = a_callback;
    l_task->callback_arg = a_callback_arg;
    l_task->next = NULL;
    
    // Add to queue
    if (a_pool->queue_tail) {
        a_pool->queue_tail->next = l_task;
    } else {
        a_pool->queue_head = l_task;
    }
    a_pool->queue_tail = l_task;
    a_pool->queue_count++;
    
    // Signal worker
    pthread_cond_signal(&a_pool->cond);
    pthread_mutex_unlock(&a_pool->mutex);
    
    return 0;
}

uint32_t dap_thread_pool_get_pending_count(dap_thread_pool_t *a_pool)
{
    if (!a_pool) {
        return 0;
    }
    
    pthread_mutex_lock(&a_pool->mutex);
    uint32_t l_count = a_pool->queue_count + a_pool->active_tasks;
    pthread_mutex_unlock(&a_pool->mutex);
    
    return l_count;
}

int dap_thread_pool_shutdown(dap_thread_pool_t *a_pool, uint32_t a_timeout_ms)
{
    if (!a_pool) {
        return -1;
    }
    
    pthread_mutex_lock(&a_pool->mutex);
    a_pool->shutdown = true;
    pthread_cond_broadcast(&a_pool->cond);
    pthread_mutex_unlock(&a_pool->mutex);
    
    // Wait for all tasks to complete with timeout
    if (a_timeout_ms > 0) {
        struct timespec l_timeout;
        clock_gettime(CLOCK_REALTIME, &l_timeout);
        l_timeout.tv_sec += a_timeout_ms / 1000;
        l_timeout.tv_nsec += (a_timeout_ms % 1000) * 1000000;
        if (l_timeout.tv_nsec >= 1000000000) {
            l_timeout.tv_sec++;
            l_timeout.tv_nsec -= 1000000000;
        }
        
        pthread_mutex_lock(&a_pool->mutex);
        while (a_pool->queue_count > 0 || a_pool->active_tasks > 0) {
            int l_ret = pthread_cond_timedwait(&a_pool->cond, &a_pool->mutex, &l_timeout);
            if (l_ret == ETIMEDOUT) {
                pthread_mutex_unlock(&a_pool->mutex);
                log_it(L_WARNING, "Thread pool shutdown timeout: %u tasks pending, %u active",
                       a_pool->queue_count, a_pool->active_tasks);
                return -2;
            }
        }
        pthread_mutex_unlock(&a_pool->mutex);
    }
    
    // Join all threads
    for (uint32_t i = 0; i < a_pool->num_threads; i++) {
        pthread_join(a_pool->threads[i], NULL);
    }
    
    log_it(L_INFO, "Thread pool shut down");
    
    return 0;
}

void dap_thread_pool_delete(dap_thread_pool_t *a_pool)
{
    if (!a_pool) {
        return;
    }
    
    // Shutdown if not already done
    if (!a_pool->shutdown) {
        dap_thread_pool_shutdown(a_pool, 5000);  // 5 second timeout
    }
    
    // Free remaining tasks
    pthread_mutex_lock(&a_pool->mutex);
    dap_thread_pool_task_t *l_task = a_pool->queue_head;
    while (l_task) {
        dap_thread_pool_task_t *l_next = l_task->next;
        DAP_DELETE(l_task);
        l_task = l_next;
    }
    pthread_mutex_unlock(&a_pool->mutex);
    
    // Cleanup
    pthread_mutex_destroy(&a_pool->mutex);
    pthread_cond_destroy(&a_pool->cond);
    DAP_DELETE(a_pool->threads);
    DAP_DELETE(a_pool);
}
