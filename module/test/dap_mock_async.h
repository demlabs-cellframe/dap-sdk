/**
 * @file dap_mock_async.h
 * @brief Asynchronous execution support for DAP Mock Framework
 * 
 * Provides lightweight async execution for mocks without requiring full event system.
 * Allows unit tests to emulate asynchronous behavior (callbacks, timers) in isolated environment.
 * 
 * Features:
 * - Thread pool for async callback execution
 * - Configurable delays (simulating network latency, I/O)
 * - Queue management for ordered execution
 * - No dependencies on dap_events (pure unit test isolation)
 * 
 * Usage:
 * 1. Initialize: dap_mock_async_init(worker_threads)
 * 2. Schedule: dap_mock_async_schedule(callback, arg, delay_ms)
 * 3. Wait: dap_mock_async_wait_completion(timeout_ms)
 * 4. Cleanup: dap_mock_async_deinit()
 * 
 * @date 2025
 * @copyright Demlabs
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Async callback function type
 * @param a_arg User-provided argument
 */
typedef void (*dap_mock_async_callback_t)(void *a_arg);

/**
 * @brief Async task handle
 */
typedef struct dap_mock_async_task dap_mock_async_task_t;

/**
 * @brief Initialize async mock system
 * @param a_worker_count Number of worker threads (0 = auto, typically 1-2 for unit tests)
 * @return 0 on success, negative on error
 */
int dap_mock_async_init(uint32_t a_worker_count);

/**
 * @brief Deinitialize async mock system
 * Waits for all pending tasks to complete
 */
void dap_mock_async_deinit(void);

/**
 * @brief Check if async system is initialized
 * @return true if initialized
 */
bool dap_mock_async_is_initialized(void);

/**
 * @brief Schedule async callback execution
 * @param a_callback Callback function to execute
 * @param a_arg Argument to pass to callback
 * @param a_delay_ms Delay before execution (milliseconds, 0 = immediate)
 * @return Task handle or NULL on error
 */
dap_mock_async_task_t* dap_mock_async_schedule(
    dap_mock_async_callback_t a_callback,
    void *a_arg,
    uint32_t a_delay_ms
);

/**
 * @brief Wait for specific task completion
 * @param a_task Task handle
 * @param a_timeout_ms Timeout in milliseconds (0 = no wait, -1 = infinite)
 * @return true if completed, false on timeout
 */
bool dap_mock_async_wait_task(dap_mock_async_task_t *a_task, int a_timeout_ms);

/**
 * @brief Wait for all pending tasks to complete
 * @param a_timeout_ms Timeout in milliseconds (-1 = infinite)
 * @return true if all completed, false on timeout
 */
bool dap_mock_async_wait_all(int a_timeout_ms);

/**
 * @brief Cancel pending task
 * @param a_task Task handle
 * @return true if cancelled, false if already executing/completed
 */
bool dap_mock_async_cancel(dap_mock_async_task_t *a_task);

/**
 * @brief Get number of pending tasks
 * @return Number of tasks in queue
 */
size_t dap_mock_async_get_pending_count(void);

/**
 * @brief Get number of completed tasks
 * @return Number of completed tasks since init
 */
size_t dap_mock_async_get_completed_count(void);

/**
 * @brief Set default delay for async operations
 * @param a_delay_ms Default delay (used when mock specifies async=true but no explicit delay)
 */
void dap_mock_async_set_default_delay(uint32_t a_delay_ms);

/**
 * @brief Get default delay
 * @return Default delay in milliseconds
 */
uint32_t dap_mock_async_get_default_delay(void);

/**
 * @brief Flush all pending tasks (execute immediately, ignore delays)
 * Useful for fast-forwarding time in tests
 */
void dap_mock_async_flush(void);

/**
 * @brief Reset statistics (pending/completed counts)
 */
void dap_mock_async_reset_stats(void);

// =============================================================================
// Advanced API
// =============================================================================

/**
 * @brief Async task state
 */
typedef enum dap_mock_async_task_state {
    DAP_MOCK_ASYNC_TASK_PENDING,    ///< Waiting in queue
    DAP_MOCK_ASYNC_TASK_DELAYED,    ///< Waiting for delay to expire
    DAP_MOCK_ASYNC_TASK_EXECUTING,  ///< Currently executing
    DAP_MOCK_ASYNC_TASK_COMPLETED,  ///< Execution finished
    DAP_MOCK_ASYNC_TASK_CANCELLED   ///< Cancelled before execution
} dap_mock_async_task_state_t;

/**
 * @brief Get task state
 * @param a_task Task handle
 * @return Task state
 */
dap_mock_async_task_state_t dap_mock_async_get_task_state(dap_mock_async_task_t *a_task);

/**
 * @brief Task completion callback (for monitoring)
 * @param a_task Completed task
 * @param a_user_arg User argument
 */
typedef void (*dap_mock_async_completion_cb_t)(dap_mock_async_task_t *a_task, void *a_user_arg);

/**
 * @brief Set global completion callback (called after each task completes)
 * @param a_callback Completion callback
 * @param a_arg User argument
 */
void dap_mock_async_set_completion_callback(
    dap_mock_async_completion_cb_t a_callback,
    void *a_arg
);

#ifdef __cplusplus
}
#endif

