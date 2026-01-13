/**
 * @file dap_mock_async.c
 * @brief Asynchronous execution support for DAP Mock Framework - Implementation
 */

#include "dap_mock_async.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>

// Task structure
struct dap_mock_async_task {
    dap_mock_async_callback_t callback;
    void *arg;
    uint32_t delay_ms;
    uint64_t execute_at_ms;  // Timestamp when task should execute
    dap_mock_async_task_state_t state;
    pthread_mutex_t state_mutex;
    pthread_cond_t state_cond;
    struct dap_mock_async_task *next;
};

// Global state
static struct {
    bool initialized;
    pthread_t *workers;
    uint32_t worker_count;
    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_cond;
    dap_mock_async_task_t *queue_head;
    dap_mock_async_task_t *queue_tail;
    size_t pending_count;
    size_t completed_count;
    uint32_t default_delay_ms;
    bool shutdown;
    dap_mock_async_completion_cb_t completion_cb;
    void *completion_cb_arg;
} s_async = {
    .initialized = false,
    .workers = NULL,
    .worker_count = 0,
    .queue_mutex = PTHREAD_MUTEX_INITIALIZER,
    .queue_cond = PTHREAD_COND_INITIALIZER,
    .queue_head = NULL,
    .queue_tail = NULL,
    .pending_count = 0,
    .completed_count = 0,
    .default_delay_ms = 10,  // 10ms default delay
    .shutdown = false,
    .completion_cb = NULL,
    .completion_cb_arg = NULL
};

// Helper: Get current time in milliseconds
static uint64_t s_get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

// Helper: Dequeue next task (caller must hold queue_mutex)
static dap_mock_async_task_t* s_dequeue_task(void) {
    if (!s_async.queue_head) {
        return NULL;
    }
    
    uint64_t now = s_get_time_ms();
    
    // Find first task that is ready to execute
    dap_mock_async_task_t *prev = NULL;
    dap_mock_async_task_t *task = s_async.queue_head;
    
    while (task) {
        if (task->execute_at_ms <= now) {
            // Remove from queue
            if (prev) {
                prev->next = task->next;
            } else {
                s_async.queue_head = task->next;
            }
            
            if (s_async.queue_tail == task) {
                s_async.queue_tail = prev;
            }
            
            task->next = NULL;
            s_async.pending_count--;
            return task;
        }
        
        prev = task;
        task = task->next;
    }
    
    return NULL;
}

// Worker thread function
static void* s_worker_thread(void *arg) {
    (void)arg;
    
    while (true) {
        pthread_mutex_lock(&s_async.queue_mutex);
        
        // Wait for tasks or shutdown
        while (!s_async.shutdown && s_async.pending_count == 0) {
            pthread_cond_wait(&s_async.queue_cond, &s_async.queue_mutex);
        }
        
        if (s_async.shutdown && s_async.pending_count == 0) {
            pthread_mutex_unlock(&s_async.queue_mutex);
            break;
        }
        
        // Try to get a ready task
        dap_mock_async_task_t *task = s_dequeue_task();
        
        if (!task) {
            // No ready tasks, wait a bit
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = 1000000;  // 1ms
            pthread_mutex_unlock(&s_async.queue_mutex);
            nanosleep(&ts, NULL);
            continue;
        }
        
        pthread_mutex_unlock(&s_async.queue_mutex);
        
        // Execute task
        pthread_mutex_lock(&task->state_mutex);
        task->state = DAP_MOCK_ASYNC_TASK_EXECUTING;
        pthread_mutex_unlock(&task->state_mutex);
        
        // Call the callback
        if (task->callback) {
            task->callback(task->arg);
        }
        
        // Mark as completed
        pthread_mutex_lock(&task->state_mutex);
        task->state = DAP_MOCK_ASYNC_TASK_COMPLETED;
        pthread_cond_broadcast(&task->state_cond);
        pthread_mutex_unlock(&task->state_mutex);
        
        // Update stats
        pthread_mutex_lock(&s_async.queue_mutex);
        s_async.completed_count++;
        pthread_mutex_unlock(&s_async.queue_mutex);
        
        // Call completion callback if set
        if (s_async.completion_cb) {
            s_async.completion_cb(task, s_async.completion_cb_arg);
        }
    }
    
    return NULL;
}

// =============================================================================
// Public API
// =============================================================================

int dap_mock_async_init(uint32_t a_worker_count) {
    if (s_async.initialized) {
        return 0;  // Already initialized
    }
    
    if (a_worker_count == 0) {
        a_worker_count = 2;  // Default for unit tests
    }
    
    s_async.worker_count = a_worker_count;
    s_async.workers = calloc(a_worker_count, sizeof(pthread_t));
    if (!s_async.workers) {
        return -ENOMEM;
    }
    
    s_async.shutdown = false;
    
    // Create worker threads
    for (uint32_t i = 0; i < a_worker_count; i++) {
        if (pthread_create(&s_async.workers[i], NULL, s_worker_thread, NULL) != 0) {
            // Cleanup on error
            s_async.shutdown = true;
            pthread_cond_broadcast(&s_async.queue_cond);
            
            for (uint32_t j = 0; j < i; j++) {
                pthread_join(s_async.workers[j], NULL);
            }
            
            free(s_async.workers);
            s_async.workers = NULL;
            return -1;
        }
    }
    
    s_async.initialized = true;
    return 0;
}

void dap_mock_async_deinit(void) {
    if (!s_async.initialized) {
        return;
    }
    
    // Signal shutdown
    pthread_mutex_lock(&s_async.queue_mutex);
    s_async.shutdown = true;
    pthread_cond_broadcast(&s_async.queue_cond);
    pthread_mutex_unlock(&s_async.queue_mutex);
    
    // Join worker threads
    for (uint32_t i = 0; i < s_async.worker_count; i++) {
        pthread_join(s_async.workers[i], NULL);
    }
    
    free(s_async.workers);
    s_async.workers = NULL;
    
    // Free remaining tasks
    pthread_mutex_lock(&s_async.queue_mutex);
    dap_mock_async_task_t *task = s_async.queue_head;
    while (task) {
        dap_mock_async_task_t *next = task->next;
        pthread_mutex_destroy(&task->state_mutex);
        pthread_cond_destroy(&task->state_cond);
        free(task);
        task = next;
    }
    s_async.queue_head = NULL;
    s_async.queue_tail = NULL;
    s_async.pending_count = 0;
    pthread_mutex_unlock(&s_async.queue_mutex);
    
    s_async.initialized = false;
}

bool dap_mock_async_is_initialized(void) {
    return s_async.initialized;
}

dap_mock_async_task_t* dap_mock_async_schedule(
    dap_mock_async_callback_t a_callback,
    void *a_arg,
    uint32_t a_delay_ms)
{
    if (!s_async.initialized) {
        return NULL;
    }
    
    dap_mock_async_task_t *task = calloc(1, sizeof(dap_mock_async_task_t));
    if (!task) {
        return NULL;
    }
    
    task->callback = a_callback;
    task->arg = a_arg;
    task->delay_ms = a_delay_ms;
    task->execute_at_ms = s_get_time_ms() + a_delay_ms;
    task->state = (a_delay_ms > 0) ? DAP_MOCK_ASYNC_TASK_DELAYED : DAP_MOCK_ASYNC_TASK_PENDING;
    task->next = NULL;
    
    pthread_mutex_init(&task->state_mutex, NULL);
    pthread_cond_init(&task->state_cond, NULL);
    
    // Add to queue
    pthread_mutex_lock(&s_async.queue_mutex);
    
    if (s_async.queue_tail) {
        s_async.queue_tail->next = task;
    } else {
        s_async.queue_head = task;
    }
    s_async.queue_tail = task;
    s_async.pending_count++;
    
    pthread_cond_signal(&s_async.queue_cond);
    pthread_mutex_unlock(&s_async.queue_mutex);
    
    return task;
}

bool dap_mock_async_wait_task(dap_mock_async_task_t *a_task, int a_timeout_ms) {
    if (!a_task) {
        return false;
    }
    
    pthread_mutex_lock(&a_task->state_mutex);
    
    if (a_task->state == DAP_MOCK_ASYNC_TASK_COMPLETED ||
        a_task->state == DAP_MOCK_ASYNC_TASK_CANCELLED) {
        pthread_mutex_unlock(&a_task->state_mutex);
        return true;
    }
    
    if (a_timeout_ms < 0) {
        // Infinite wait
        while (a_task->state != DAP_MOCK_ASYNC_TASK_COMPLETED &&
               a_task->state != DAP_MOCK_ASYNC_TASK_CANCELLED) {
            pthread_cond_wait(&a_task->state_cond, &a_task->state_mutex);
        }
        pthread_mutex_unlock(&a_task->state_mutex);
        return true;
    } else if (a_timeout_ms == 0) {
        // No wait
        bool completed = (a_task->state == DAP_MOCK_ASYNC_TASK_COMPLETED ||
                         a_task->state == DAP_MOCK_ASYNC_TASK_CANCELLED);
        pthread_mutex_unlock(&a_task->state_mutex);
        return completed;
    } else {
        // Timed wait
        struct timespec ts;
        struct timeval tv;
        gettimeofday(&tv, NULL);
        ts.tv_sec = tv.tv_sec + a_timeout_ms / 1000;
        ts.tv_nsec = (tv.tv_usec + (a_timeout_ms % 1000) * 1000) * 1000;
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000;
        }
        
        int ret = 0;
        while (a_task->state != DAP_MOCK_ASYNC_TASK_COMPLETED &&
               a_task->state != DAP_MOCK_ASYNC_TASK_CANCELLED &&
               ret != ETIMEDOUT) {
            ret = pthread_cond_timedwait(&a_task->state_cond, &a_task->state_mutex, &ts);
        }
        
        bool completed = (a_task->state == DAP_MOCK_ASYNC_TASK_COMPLETED ||
                         a_task->state == DAP_MOCK_ASYNC_TASK_CANCELLED);
        pthread_mutex_unlock(&a_task->state_mutex);
        return completed;
    }
}

bool dap_mock_async_wait_all(int a_timeout_ms) {
    if (!s_async.initialized) {
        return true;
    }
    
    uint64_t deadline_ms = (a_timeout_ms < 0) ? UINT64_MAX :
                           (s_get_time_ms() + a_timeout_ms);
    
    while (true) {
        pthread_mutex_lock(&s_async.queue_mutex);
        size_t pending = s_async.pending_count;
        pthread_mutex_unlock(&s_async.queue_mutex);
        
        if (pending == 0) {
            return true;
        }
        
        if (a_timeout_ms >= 0 && s_get_time_ms() >= deadline_ms) {
            return false;
        }
        
        usleep(1000);  // 1ms
    }
}

bool dap_mock_async_cancel(dap_mock_async_task_t *a_task) {
    if (!a_task) {
        return false;
    }
    
    pthread_mutex_lock(&a_task->state_mutex);
    
    if (a_task->state == DAP_MOCK_ASYNC_TASK_PENDING ||
        a_task->state == DAP_MOCK_ASYNC_TASK_DELAYED) {
        a_task->state = DAP_MOCK_ASYNC_TASK_CANCELLED;
        pthread_cond_broadcast(&a_task->state_cond);
        pthread_mutex_unlock(&a_task->state_mutex);
        
        // Remove from queue
        pthread_mutex_lock(&s_async.queue_mutex);
        dap_mock_async_task_t *prev = NULL;
        dap_mock_async_task_t *curr = s_async.queue_head;
        
        while (curr) {
            if (curr == a_task) {
                if (prev) {
                    prev->next = curr->next;
                } else {
                    s_async.queue_head = curr->next;
                }
                
                if (s_async.queue_tail == curr) {
                    s_async.queue_tail = prev;
                }
                
                s_async.pending_count--;
                break;
            }
            prev = curr;
            curr = curr->next;
        }
        
        pthread_mutex_unlock(&s_async.queue_mutex);
        return true;
    }
    
    pthread_mutex_unlock(&a_task->state_mutex);
    return false;
}

size_t dap_mock_async_get_pending_count(void) {
    pthread_mutex_lock(&s_async.queue_mutex);
    size_t count = s_async.pending_count;
    pthread_mutex_unlock(&s_async.queue_mutex);
    return count;
}

size_t dap_mock_async_get_completed_count(void) {
    pthread_mutex_lock(&s_async.queue_mutex);
    size_t count = s_async.completed_count;
    pthread_mutex_unlock(&s_async.queue_mutex);
    return count;
}

void dap_mock_async_set_default_delay(uint32_t a_delay_ms) {
    s_async.default_delay_ms = a_delay_ms;
}

uint32_t dap_mock_async_get_default_delay(void) {
    return s_async.default_delay_ms;
}

void dap_mock_async_flush(void) {
    pthread_mutex_lock(&s_async.queue_mutex);
    
    // Set all tasks to execute immediately
    dap_mock_async_task_t *task = s_async.queue_head;
    while (task) {
        task->execute_at_ms = 0;
        pthread_mutex_lock(&task->state_mutex);
        if (task->state == DAP_MOCK_ASYNC_TASK_DELAYED) {
            task->state = DAP_MOCK_ASYNC_TASK_PENDING;
        }
        pthread_mutex_unlock(&task->state_mutex);
        task = task->next;
    }
    
    pthread_cond_broadcast(&s_async.queue_cond);
    pthread_mutex_unlock(&s_async.queue_mutex);
}

void dap_mock_async_reset_stats(void) {
    pthread_mutex_lock(&s_async.queue_mutex);
    s_async.completed_count = 0;
    pthread_mutex_unlock(&s_async.queue_mutex);
}

dap_mock_async_task_state_t dap_mock_async_get_task_state(dap_mock_async_task_t *a_task) {
    if (!a_task) {
        return DAP_MOCK_ASYNC_TASK_CANCELLED;
    }
    
    pthread_mutex_lock(&a_task->state_mutex);
    dap_mock_async_task_state_t state = a_task->state;
    pthread_mutex_unlock(&a_task->state_mutex);
    
    return state;
}

void dap_mock_async_set_completion_callback(
    dap_mock_async_completion_cb_t a_callback,
    void *a_arg)
{
    s_async.completion_cb = a_callback;
    s_async.completion_cb_arg = a_arg;
}

