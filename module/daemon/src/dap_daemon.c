/**
 * @file dap_daemon.c
 * @brief Daemon utilities implementation
 *
 * Copyright (c) 2017-2026 Demlabs
 * License: GNU GPL v3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifndef DAP_OS_WINDOWS
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif

#include "dap_common.h"
#include "dap_interval_timer.h"
#include "dap_daemon.h"

#define LOG_TAG "dap_daemon"

// =============================================================================
// Daemon Lifecycle
// =============================================================================

int dap_daemon_init(void)
{
    return 0;
}

void dap_daemon_deinit(void)
{
    // Cleanup if needed
}

#ifndef DAP_OS_WINDOWS
int dap_daemon_start(const char *a_pid_file)
{
    pid_t l_pid;

    // First fork
    l_pid = fork();
    if (l_pid < 0) {
        log_it(L_CRITICAL, "First fork failed: %s", strerror(errno));
        return -1;
    }
    if (l_pid > 0) {
        // Parent exits
        exit(EXIT_SUCCESS);
    }

    // Create new session
    if (setsid() < 0) {
        log_it(L_CRITICAL, "setsid failed: %s", strerror(errno));
        return -2;
    }

    // Second fork (prevent acquiring controlling terminal)
    l_pid = fork();
    if (l_pid < 0) {
        log_it(L_CRITICAL, "Second fork failed: %s", strerror(errno));
        return -3;
    }
    if (l_pid > 0) {
        exit(EXIT_SUCCESS);
    }

    // Set file permissions
    umask(0);

    // Change to root directory
    if (chdir("/") < 0) {
        log_it(L_WARNING, "chdir to / failed: %s", strerror(errno));
    }

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect to /dev/null
    int l_null_fd = open("/dev/null", O_RDWR);
    if (l_null_fd >= 0) {
        dup2(l_null_fd, STDIN_FILENO);
        dup2(l_null_fd, STDOUT_FILENO);
        dup2(l_null_fd, STDERR_FILENO);
        if (l_null_fd > STDERR_FILENO) {
            close(l_null_fd);
        }
    }

    // Create PID file
    if (a_pid_file) {
        if (dap_pid_file_create(a_pid_file) < 0) {
            log_it(L_ERROR, "Failed to create PID file: %s", a_pid_file);
        }
    }

    log_it(L_NOTICE, "Daemon started, PID %d", getpid());
    return 0;
}

void dap_daemon_stop(const char *a_pid_file)
{
    if (a_pid_file) {
        dap_pid_file_remove(a_pid_file);
    }
    log_it(L_NOTICE, "Daemon stopped");
}
#else
// Windows stubs
int dap_daemon_start(const char *a_pid_file)
{
    UNUSED(a_pid_file);
    log_it(L_WARNING, "Daemonization not supported on Windows");
    return 0;
}

void dap_daemon_stop(const char *a_pid_file)
{
    UNUSED(a_pid_file);
}
#endif

// =============================================================================
// PID File Management
// =============================================================================

int dap_pid_file_create(const char *a_pid_file)
{
    if (!a_pid_file) {
        return -1;
    }

    FILE *l_fp = fopen(a_pid_file, "w");
    if (!l_fp) {
        log_it(L_ERROR, "Cannot create PID file '%s': %s", a_pid_file, strerror(errno));
        return -2;
    }

#ifdef DAP_OS_WINDOWS
    fprintf(l_fp, "%lu\n", (unsigned long)GetCurrentProcessId());
#else
    fprintf(l_fp, "%d\n", getpid());
#endif
    fclose(l_fp);

    log_it(L_DEBUG, "PID file created: %s", a_pid_file);
    return 0;
}

int dap_pid_file_remove(const char *a_pid_file)
{
    if (!a_pid_file) {
        return -1;
    }

    if (remove(a_pid_file) != 0 && errno != ENOENT) {
        log_it(L_WARNING, "Cannot remove PID file '%s': %s", a_pid_file, strerror(errno));
        return -2;
    }

    log_it(L_DEBUG, "PID file removed: %s", a_pid_file);
    return 0;
}

int dap_pid_file_read(const char *a_pid_file)
{
    if (!a_pid_file) {
        return -1;
    }

    FILE *l_fp = fopen(a_pid_file, "r");
    if (!l_fp) {
        return -1;
    }

    int l_pid = 0;
    if (fscanf(l_fp, "%d", &l_pid) != 1) {
        fclose(l_fp);
        return -1;
    }
    fclose(l_fp);

    return l_pid;
}

bool dap_pid_file_is_running(const char *a_pid_file)
{
    int l_pid = dap_pid_file_read(a_pid_file);
    if (l_pid <= 0) {
        return false;
    }

#ifdef DAP_OS_WINDOWS
    HANDLE l_proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)l_pid);
    if (l_proc) {
        CloseHandle(l_proc);
        return true;
    }
    return false;
#else
    // Check if process exists by sending signal 0
    return (kill(l_pid, 0) == 0);
#endif
}

// =============================================================================
// Signal Handling
// =============================================================================

#ifndef DAP_OS_WINDOWS
static dap_signal_handler_t s_shutdown_handler = NULL;
static dap_signal_handler_t s_reload_handler = NULL;

static void s_signal_handler(int a_signum)
{
    switch (a_signum) {
        case SIGTERM:
        case SIGINT:
            log_it(L_NOTICE, "Received signal %d, initiating shutdown...", a_signum);
            if (s_shutdown_handler) {
                s_shutdown_handler(a_signum);
            }
            break;
        case SIGHUP:
            log_it(L_NOTICE, "Received SIGHUP, reloading configuration...");
            if (s_reload_handler) {
                s_reload_handler(a_signum);
            }
            break;
        default:
            break;
    }
}

int dap_daemon_setup_signals(dap_signal_handler_t a_shutdown_handler,
                              dap_signal_handler_t a_reload_handler)
{
    s_shutdown_handler = a_shutdown_handler;
    s_reload_handler = a_reload_handler;

    struct sigaction l_sa;
    memset(&l_sa, 0, sizeof(l_sa));
    l_sa.sa_handler = s_signal_handler;
    sigemptyset(&l_sa.sa_mask);
    l_sa.sa_flags = 0;

    if (sigaction(SIGTERM, &l_sa, NULL) < 0) {
        log_it(L_ERROR, "Failed to setup SIGTERM handler: %s", strerror(errno));
        return -1;
    }
    if (sigaction(SIGINT, &l_sa, NULL) < 0) {
        log_it(L_ERROR, "Failed to setup SIGINT handler: %s", strerror(errno));
        return -2;
    }
    if (sigaction(SIGHUP, &l_sa, NULL) < 0) {
        log_it(L_ERROR, "Failed to setup SIGHUP handler: %s", strerror(errno));
        return -3;
    }

    // Ignore SIGPIPE
    l_sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &l_sa, NULL) < 0) {
        log_it(L_WARNING, "Failed to ignore SIGPIPE: %s", strerror(errno));
    }

    log_it(L_DEBUG, "Signal handlers installed");
    return 0;
}

int dap_daemon_signals_block(void)
{
    sigset_t l_set;
    sigemptyset(&l_set);
    sigaddset(&l_set, SIGTERM);
    sigaddset(&l_set, SIGINT);
    sigaddset(&l_set, SIGHUP);
    return pthread_sigmask(SIG_BLOCK, &l_set, NULL);
}

int dap_daemon_signals_unblock(void)
{
    sigset_t l_set;
    sigemptyset(&l_set);
    sigaddset(&l_set, SIGTERM);
    sigaddset(&l_set, SIGINT);
    sigaddset(&l_set, SIGHUP);
    return pthread_sigmask(SIG_UNBLOCK, &l_set, NULL);
}
#else
// Windows stubs
int dap_daemon_setup_signals(dap_signal_handler_t a_shutdown_handler,
                              dap_signal_handler_t a_reload_handler)
{
    UNUSED(a_shutdown_handler);
    UNUSED(a_reload_handler);
    log_it(L_DEBUG, "Signal handling not implemented on Windows");
    return 0;
}

int dap_daemon_signals_block(void) { return 0; }
int dap_daemon_signals_unblock(void) { return 0; }
#endif

// =============================================================================
// Log Management
// =============================================================================

static void s_log_cleaner_callback(void *a_max_size)
{
    size_t l_max_size_mb = DAP_POINTER_TO_SIZE(a_max_size);
    
    // Get current log file size
    FILE *l_log_file = dap_log_get_file();
    if (!l_log_file) {
        return;
    }

    long l_log_size = ftell(l_log_file);
    if (l_log_size < 0) {
        log_it(L_ERROR, "Can't tell log file size: %s", strerror(errno));
        return;
    }

    size_t l_size_mb = (size_t)l_log_size / (1024 * 1024);
    if (l_size_mb > l_max_size_mb) {
        log_it(L_NOTICE, "Log file size %zu MB exceeds limit %zu MB, rotation needed",
               l_size_mb, l_max_size_mb);
        // Log rotation handled by dap_common
        dap_log_reopen();
    }
}

void dap_daemon_enable_log_cleaner(size_t a_timeout_ms, size_t a_max_size_mb)
{
    dap_interval_timer_create(a_timeout_ms, s_log_cleaner_callback, 
                               DAP_SIZE_TO_POINTER(a_max_size_mb));
    log_it(L_INFO, "Log cleaner enabled: check every %zu ms, max size %zu MB",
           a_timeout_ms, a_max_size_mb);
}
