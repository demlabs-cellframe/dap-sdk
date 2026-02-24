/**
 * @file dap_daemon.h
 * @brief Daemon utilities - daemonization, signals, PID files, log management
 *
 * Copyright (c) 2017-2026 Demlabs
 * License: GNU GPL v3
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Signal handler callback type
 */
typedef void (*dap_signal_handler_t)(int a_signum);

/**
 * @brief Initialize daemon subsystem
 * @return 0 on success, negative on error
 */
int dap_daemon_init(void);

/**
 * @brief Cleanup daemon subsystem
 */
void dap_daemon_deinit(void);

/**
 * @brief Daemonize the current process
 * 
 * Performs standard Unix daemonization:
 * - Fork and exit parent
 * - Create new session (setsid)
 * - Change working directory to /
 * - Close standard file descriptors
 * - Redirect stdin/stdout/stderr to /dev/null
 *
 * @param a_pid_file Path to PID file (can be NULL)
 * @return 0 on success, negative on error
 */
int dap_daemon_start(const char *a_pid_file);

/**
 * @brief Stop daemon and cleanup
 * @param a_pid_file Path to PID file to remove (can be NULL)
 */
void dap_daemon_stop(const char *a_pid_file);

// =============================================================================
// PID File Management
// =============================================================================

/**
 * @brief Create PID file with current process ID
 * @param a_pid_file Path to PID file
 * @return 0 on success, negative on error
 */
int dap_pid_file_create(const char *a_pid_file);

/**
 * @brief Remove PID file
 * @param a_pid_file Path to PID file
 * @return 0 on success, negative on error
 */
int dap_pid_file_remove(const char *a_pid_file);

/**
 * @brief Read PID from file
 * @param a_pid_file Path to PID file
 * @return PID on success, -1 on error
 */
int dap_pid_file_read(const char *a_pid_file);

/**
 * @brief Check if process with PID from file is running
 * @param a_pid_file Path to PID file
 * @return true if running, false otherwise
 */
bool dap_pid_file_is_running(const char *a_pid_file);

// =============================================================================
// Signal Handling
// =============================================================================

/**
 * @brief Setup default signal handlers for daemon
 * 
 * Installs handlers for:
 * - SIGTERM, SIGINT: graceful shutdown
 * - SIGHUP: reload configuration (if handler provided)
 * - SIGPIPE: ignore
 *
 * @param a_shutdown_handler Handler for shutdown signals (can be NULL for default)
 * @param a_reload_handler Handler for SIGHUP reload (can be NULL to ignore)
 * @return 0 on success, negative on error
 */
int dap_daemon_setup_signals(dap_signal_handler_t a_shutdown_handler,
                              dap_signal_handler_t a_reload_handler);

/**
 * @brief Block signals during critical section
 * @return 0 on success, negative on error
 */
int dap_daemon_signals_block(void);

/**
 * @brief Unblock signals after critical section
 * @return 0 on success, negative on error
 */
int dap_daemon_signals_unblock(void);

// =============================================================================
// Log Management
// =============================================================================

/**
 * @brief Enable periodic log file cleaner
 * 
 * Creates an interval timer that checks log file size and
 * rotates/truncates when it exceeds the maximum size.
 *
 * @param a_timeout_ms Check interval in milliseconds
 * @param a_max_size_mb Maximum log file size in megabytes
 */
void dap_daemon_enable_log_cleaner(size_t a_timeout_ms, size_t a_max_size_mb);

#ifdef __cplusplus
}
#endif
