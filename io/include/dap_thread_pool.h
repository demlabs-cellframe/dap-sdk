/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * CellFrame       https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 *
 * This file is part of DAP SDK
 *
 * DAP SDK is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "dap_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Thread pool handle (opaque)
 */
typedef struct dap_thread_pool dap_thread_pool_t;

/**
 * @brief Task function type
 * @param a_arg User argument
 * @return Task result (can be NULL)
 */
typedef void* (*dap_thread_pool_task_func_t)(void *a_arg);

/**
 * @brief Task completion callback type
 * @param a_result Task result from task function
 * @param a_arg User argument
 */
typedef void (*dap_thread_pool_callback_t)(void *a_result, void *a_arg);

/**
 * @brief Create thread pool
 * @param a_num_threads Number of worker threads (0 = auto-detect CPU count)
 * @param a_queue_size Max task queue size (0 = unlimited)
 * @return Thread pool handle, or NULL on error
 */
dap_thread_pool_t* dap_thread_pool_create(uint32_t a_num_threads, uint32_t a_queue_size);

/**
 * @brief Submit task to thread pool
 * @param a_pool Thread pool handle
 * @param a_task_func Task function to execute
 * @param a_task_arg Task argument
 * @param a_callback Completion callback (called from worker thread, can be NULL)
 * @param a_callback_arg Callback argument
 * @return 0 on success, negative on error
 */
int dap_thread_pool_submit(dap_thread_pool_t *a_pool,
                           dap_thread_pool_task_func_t a_task_func,
                           void *a_task_arg,
                           dap_thread_pool_callback_t a_callback,
                           void *a_callback_arg);

/**
 * @brief Get number of pending tasks
 * @param a_pool Thread pool handle
 * @return Number of pending tasks
 */
uint32_t dap_thread_pool_get_pending_count(dap_thread_pool_t *a_pool);

/**
 * @brief Shutdown thread pool (wait for all tasks to complete)
 * @param a_pool Thread pool handle
 * @param a_timeout_ms Timeout in milliseconds (0 = wait forever)
 * @return 0 on success, negative on timeout/error
 */
int dap_thread_pool_shutdown(dap_thread_pool_t *a_pool, uint32_t a_timeout_ms);

/**
 * @brief Delete thread pool (force shutdown if not already shut down)
 * @param a_pool Thread pool handle
 */
void dap_thread_pool_delete(dap_thread_pool_t *a_pool);

#ifdef __cplusplus
}
#endif
