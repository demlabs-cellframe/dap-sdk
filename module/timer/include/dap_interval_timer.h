/**
 * @file dap_interval_timer.h
 * @brief Interval timer - periodic callback execution
 *
 * Cross-platform interval timer implementation using:
 * - Linux: POSIX timer_create with SIGEV_THREAD
 * - Windows: CreateTimerQueueTimer
 * - macOS: dispatch_source_create
 *
 * Copyright (c) 2017-2026 Demlabs
 * License: GNU GPL v3
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Opaque timer handle
typedef void *dap_interval_timer_t;

/// Timer callback function type
typedef void (*dap_timer_callback_t)(void *a_param);

/**
 * @brief Initialize interval timer subsystem
 * Must be called before creating any timers
 */
void dap_interval_timer_init(void);

/**
 * @brief Deinitialize interval timer subsystem
 * Stops and frees all active timers
 */
void dap_interval_timer_deinit(void);

/**
 * @brief Create a periodic interval timer
 * @param a_msec Timer period in milliseconds
 * @param a_callback Function to call on each timer tick
 * @param a_param User parameter passed to callback
 * @return Timer handle on success, NULL on failure
 */
dap_interval_timer_t dap_interval_timer_create(unsigned int a_msec, 
                                                dap_timer_callback_t a_callback, 
                                                void *a_param);

/**
 * @brief Delete a timer and free resources
 * @param a_timer Timer handle
 */
void dap_interval_timer_delete(dap_interval_timer_t a_timer);

/**
 * @brief Disable timer without deleting (internal use)
 * @param a_timer Timer handle
 * @return 0 on success, non-zero on error
 */
int dap_interval_timer_disable(dap_interval_timer_t a_timer);

#ifdef __cplusplus
}
#endif
