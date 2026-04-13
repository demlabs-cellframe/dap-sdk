/*
 * Secure memory wiping — prevents compiler from optimizing out zeroing.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define __STDC_WANT_LIB_EXT1__ 1
#include <string.h>
#include <stdlib.h>
#ifdef HAVE_EXPLICIT_BZERO
#include <strings.h>
#endif
#include "dap_memwipe.h"

#if defined(_MSC_VER)
#define DAP_MEMWIPE_BARRIER \
    __asm;
#else
#define DAP_MEMWIPE_BARRIER \
    __asm__ __volatile__("" : : "r"(ptr) : "memory");
#endif

#ifdef HAVE_MEMSET_S

void *dap_memwipe(void *ptr, size_t n)
{
    if (memset_s(ptr, n, 0, n))
        abort();
    DAP_MEMWIPE_BARRIER
    return ptr;
}

#elif defined(HAVE_EXPLICIT_BZERO)

void *dap_memwipe(void *ptr, size_t n)
{
    explicit_bzero(ptr, n);
    DAP_MEMWIPE_BARRIER
    return ptr;
}

#else

static void s_memory_cleanse(void *ptr, size_t len)
{
    memset(ptr, 0, len);
    DAP_MEMWIPE_BARRIER
}

void *dap_memwipe(void *ptr, size_t n)
{
    s_memory_cleanse(ptr, n);
    DAP_MEMWIPE_BARRIER
    return ptr;
}

#endif
