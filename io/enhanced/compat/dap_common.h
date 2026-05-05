/*
 * Minimal dap_common.h shim for standalone enhanced builds.
 * Provides only the macros and helpers actually used by the enhanced modules.
 */
#pragma once

#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

#define DAP_STATIC_INLINE              static inline
#define UNUSED_ARG                     __attribute__((unused))

#define DAP_NEW_Z_SIZE(type, size)     ((type *)calloc(1, (size)))
#define DAP_DEL_Z(a)                   do { free(a); (a) = NULL; } while (0)

#define dap_return_val_if_pass(e, v)   do { if (e) return (v); } while (0)
#define dap_return_val_if_fail(e, v)   do { if (!(e)) return (v); } while (0)

static inline long dap_pagesize(void)
{
    static volatile long s_ps = 0;
    long l_ps = s_ps;
    if (__builtin_expect(!l_ps, 0)) {
        l_ps = sysconf(_SC_PAGESIZE);
        if (l_ps <= 0) l_ps = 4096;
        s_ps = l_ps;
    }
    return l_ps;
}
