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
 *
 * DAP SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with any DAP SDK based project. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <pthread.h>
#include <stdint.h>
#include <stdbool.h>
#include "dap_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Thread handle type
 */
typedef pthread_t dap_thread_t;

/**
 * @brief Thread ID type
 */
typedef uint64_t dap_thread_id_t;

/**
 * @brief Thread function type
 * @param a_arg User argument
 * @return Thread exit code
 */
typedef void* (*dap_thread_func_t)(void *a_arg);

/**
 * @brief Create and start a new thread
 * @param a_func Thread function
 * @param a_arg User argument
 * @return Thread handle, or 0 on error
 */
dap_thread_t dap_thread_create(dap_thread_func_t a_func, void *a_arg);

/**
 * @brief Wait for thread to finish
 * @param a_thread Thread handle
 * @param a_retval Pointer to store thread return value (can be NULL)
 * @return 0 on success, negative on error
 */
int dap_thread_join(dap_thread_t a_thread, void **a_retval);

/**
 * @brief Get current thread ID
 * @return Current thread ID
 */
dap_thread_id_t dap_thread_get_id(void);

/**
 * @brief Set thread name (for debugging)
 * @param a_thread Thread handle
 * @param a_name Thread name (max 15 chars on Linux)
 * @return 0 on success, negative on error
 */
int dap_thread_set_name(dap_thread_t a_thread, const char *a_name);

/**
 * @brief Detach thread (auto-cleanup on exit)
 * @param a_thread Thread handle
 * @return 0 on success, negative on error
 */
int dap_thread_detach(dap_thread_t a_thread);

/**
 * @brief Set CPU affinity for thread (bind to specific CPU core)
 * @param a_thread Thread handle
 * @param a_cpu_id CPU core ID (0-based)
 * @return 0 on success, negative on error
 */
int dap_thread_set_affinity(dap_thread_t a_thread, uint32_t a_cpu_id);

#ifdef __cplusplus
}
#endif
