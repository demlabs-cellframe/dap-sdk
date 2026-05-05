/*
 * Authors:
 * Constantin Papizh <papizh.konstantin@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif
#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include "dap_common.h"

#ifdef DAP_OS_WINDOWS
/* Win8+ (0x0602) required for WaitOnAddress / WakeByAddress* futex API */
#  if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0602
#    undef  _WIN32_WINNT
#    define _WIN32_WINNT 0x0602
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/mman.h>
#  include <unistd.h>
#  include <time.h>
#  include <linux/futex.h>  /* FUTEX_WAIT, FUTEX_WAKE, FUTEX_WAIT_BITSET */
#  include <sys/syscall.h>  /* SYS_futex */
#endif

/* File descriptor type: SOCKET on Windows, int on POSIX */
#ifdef DAP_OS_WINDOWS
typedef SOCKET dap_fd_t;
#else
typedef int    dap_fd_t;
#endif

/* Thread-local storage qualifier */
#ifdef _MSC_VER
#  define DAP_THREADLOCAL __declspec(thread)
#else
#  define DAP_THREADLOCAL __thread
#endif

/*
 * Alignment rounding: a_x up to the nearest multiple of a_align.
 * a_align MUST be a power of two.
 */
#define DAP_ALIGN_UP(a_x, a_align) \
    (((a_x) + (a_align) - 1) & ~((size_t)(a_align) - 1))

/* Human-readable size literals: Kbytes(4) = 4096, Mbytes(2) = 2097152 */
#define Kbytes(n) ((size_t)(n) << 10)
#define Mbytes(n) ((size_t)(n) << 20)

/* Default huge page size (2 MiB on x86_64); override at build time */
#ifndef DAP_HUGEPAGE_SIZE
#  define DAP_HUGEPAGE_SIZE Mbytes(2)
#endif
#ifndef DAP_OS_WINDOWS
/* MAP_HUGETLB may be absent from older kernel headers */
#  ifndef MAP_HUGETLB
#    define MAP_HUGETLB 0x40000
#  endif
#endif

/*
 * Spin-wait hint for the CPU, reduces power consumption and contention
 * on the memory bus during spin loops.
 */
DAP_STATIC_INLINE void dap_cpu_relax(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#elif defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

/* ------------------------------------------------------------------ */
/*  Futex — lightweight kernel-assisted wait/wake                      */
/*                                                                     */
/*  Windows:  WaitOnAddress / WakeByAddress* (Win8+)                   */
/*  Linux:    SYS_futex with FUTEX_PRIVATE_FLAG (process-local)        */
/*                                                                     */
/*  dap_futex_wait(addr, exp)                                          */
/*      Sleep while *addr == exp; return on wake or value change.      */
/*      EINTR is retried transparently.                                */
/*                                                                     */
/*  dap_futex_wait_timed(addr, exp, ms) -> bool                        */
/*      Same with millisecond timeout.  Returns true if woken or       */
/*      value changed, false on timeout.                               */
/*      Timeout sentinel: 0xFFFFFFFF (infinite, delegates to           */
/*      dap_futex_wait).                                               */
/*                                                                     */
/*  dap_futex_wake(addr, count)                                        */
/*      Wake up to count waiters on addr.                              */
/* ------------------------------------------------------------------ */

#ifdef DAP_OS_WINDOWS

DAP_STATIC_INLINE void dap_futex_wait(void *a_addr, uint32_t a_expected)
{
    WaitOnAddress(a_addr, &a_expected, sizeof(uint32_t), INFINITE);
}

DAP_STATIC_INLINE bool dap_futex_wait_timed(void *a_addr, uint32_t a_expected, uint32_t a_ms)
{
    return WaitOnAddress(a_addr, &a_expected, sizeof(uint32_t), a_ms);
}

DAP_STATIC_INLINE void dap_futex_wake(void *a_addr, int a_count)
{
    if (a_count <= 1)
        WakeByAddressSingle(a_addr);
    else
        WakeByAddressAll(a_addr);
}

#else /* Linux / POSIX */

/* Low-level futex syscall wrapper; FUTEX_PRIVATE_FLAG = process-local */
DAP_STATIC_INLINE int s_futex_op(void *a_addr, int a_op, uint32_t a_val,
                                  const struct timespec *a_ts)
{
    return (int)syscall(SYS_futex, a_addr, a_op | FUTEX_PRIVATE_FLAG,
                        a_val, a_ts, NULL, 0);
}

/* Block while *a_addr == a_expected; EINTR is silently retried */
DAP_STATIC_INLINE void dap_futex_wait(void *a_addr, uint32_t a_expected)
{
    while (s_futex_op(a_addr, FUTEX_WAIT, a_expected, NULL) < 0 && errno == EINTR)
        ;
}

/*
 * Timed futex wait.  Returns true if woken or value changed (EAGAIN),
 * false on timeout.  Sentinel 0xFFFFFFFF = infinite (delegates to
 * dap_futex_wait).
 *
 * Preferred path uses FUTEX_WAIT_BITSET with an absolute CLOCK_MONOTONIC
 * deadline: clock_gettime is called once upfront, and the kernel checks
 * the deadline internally.  On EINTR the syscall is simply re-issued
 * with the same absolute timespec — no user-space time recomputation.
 *
 * Fallback (no FUTEX_WAIT_BITSET): plain FUTEX_WAIT takes a relative
 * timeout, so each EINTR requires a fresh clock_gettime to recalculate
 * the remaining interval.
 */
DAP_STATIC_INLINE bool dap_futex_wait_timed(void *a_addr, uint32_t a_expected,
                                             uint32_t a_ms)
{
    if (a_ms == 0xFFFFFFFFU) {              /* DAP_TIMEOUT_INFINITE */
        dap_futex_wait(a_addr, a_expected);
        return true;
    }
#ifdef FUTEX_WAIT_BITSET
    /* Absolute timeout: one clock_gettime, transparent EINTR retry */
    struct timespec l_now;
    clock_gettime(CLOCK_MONOTONIC, &l_now);
    uint64_t l_ns = (uint64_t)l_now.tv_nsec + (uint64_t)a_ms * 1000000ULL;
    struct timespec l_abs = {
        .tv_sec  = l_now.tv_sec + (time_t)(l_ns / 1000000000ULL),
        .tv_nsec = (long)(l_ns % 1000000000ULL)
    };
    int l_rc;
    do {                                    /* EINTR: re-issue with same deadline */
        l_rc = (int)syscall(SYS_futex, a_addr,
                            FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG,
                            a_expected, &l_abs, NULL,
                            FUTEX_BITSET_MATCH_ANY);
    } while (l_rc < 0 && errno == EINTR);
    return l_rc == 0 || errno == EAGAIN;    /* woken or value changed */
#else
    /* Relative timeout fallback: must recompute remaining time on EINTR */
    struct timespec l_now;
    clock_gettime(CLOCK_MONOTONIC, &l_now);
    uint64_t l_deadline_ns = (uint64_t)l_now.tv_sec * 1000000000ULL
                           + (uint64_t)l_now.tv_nsec
                           + (uint64_t)a_ms * 1000000ULL;
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &l_now);
        uint64_t l_now_ns = (uint64_t)l_now.tv_sec * 1000000000ULL
                          + (uint64_t)l_now.tv_nsec;
        if (l_now_ns >= l_deadline_ns) return false;    /* timed out */
        uint64_t l_remain = l_deadline_ns - l_now_ns;
        struct timespec l_ts = {
            .tv_sec  = (time_t)(l_remain / 1000000000ULL),
            .tv_nsec = (long)(l_remain % 1000000000ULL)
        };
        int l_rc = s_futex_op(a_addr, FUTEX_WAIT, a_expected, &l_ts);
        if (l_rc == 0)                return true;      /* woken */
        if (errno == EAGAIN)          return true;      /* value changed */
        if (errno == ETIMEDOUT)       return false;
        if (errno != EINTR)           return false;     /* unexpected error */
    }
#endif
}

/* Wake up to a_count threads blocked in dap_futex_wait on a_addr */
DAP_STATIC_INLINE void dap_futex_wake(void *a_addr, int a_count)
{
    s_futex_op(a_addr, FUTEX_WAKE, (uint32_t)a_count, NULL);
}

#endif /* DAP_OS_WINDOWS */
