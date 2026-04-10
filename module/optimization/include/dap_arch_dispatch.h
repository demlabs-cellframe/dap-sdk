/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
 * This file is part of DAP SDK the open source project
 *
 *    DAP SDK is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    DAP SDK is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file dap_arch_dispatch.h
 * @brief Generic architecture dispatch macros for runtime SIMD selection.
 *
 * Design: static inline wrappers in headers for zero-overhead dispatch.
 * The function pointer check + indirect call is inlined at each call site,
 * eliminating the extra direct→indirect double-call overhead.
 *
 * Hot path (after first call): one predicted branch + one indirect call.
 * Cold path (first call only): branch + init + indirect call.
 *
 * === Header (public API) ===
 *
 *   #include "dap_arch_dispatch.h"
 *
 *   DAP_DISPATCH_DECLARE(dap_ntt16_forward, void,
 *                        int16_t *, const dap_ntt_params16_t *);
 *   extern void dap_ntt_dispatch_init(void);
 *
 *   static inline void dap_ntt16_forward(int16_t *a_coeffs,
 *                                        const dap_ntt_params16_t *a_params) {
 *       DAP_DISPATCH_ENSURE(dap_ntt16_forward, dap_ntt_dispatch_init);
 *       dap_ntt16_forward_ptr(a_coeffs, a_params);
 *   }
 *
 * === Source (.c file) ===
 *
 *   DAP_DISPATCH_DEFINE(dap_ntt16_forward);
 *
 *   void dap_ntt_dispatch_init(void) {
 *       DAP_DISPATCH_DEFAULT(dap_ntt16_forward, dap_ntt16_forward_ref);
 *       DAP_DISPATCH_ARCH_SELECT;
 *       DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   dap_ntt16_forward, dap_ntt16_forward_avx2);
 *       DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, dap_ntt16_forward, dap_ntt16_forward_avx512);
 *       DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   dap_ntt16_forward, dap_ntt16_forward_neon);
 *   }
 */

#pragma once

#include "dap_cpu_arch.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                        Platform detection guards                           */
/* ========================================================================== */

/* DAP_PLATFORM_X86 / DAP_PLATFORM_ARM defined in dap_cpu_arch.h */

/* ========================================================================== */
/*          Function pointer declaration (header) / definition (source)       */
/* ========================================================================== */

/**
 * Declare extern function pointer type + storage.
 * Place in public headers alongside static inline wrappers.
 *
 * Generates:
 *   typedef ret_t (*name_fn_t)(args...);
 *   extern name_fn_t name_ptr;
 */
#define DAP_DISPATCH_DECLARE(name, ret_t, ...)    \
    typedef ret_t (*name##_fn_t)(__VA_ARGS__);    \
    extern name##_fn_t name##_ptr

/**
 * Define function pointer storage. Place in exactly one .c file.
 */
#define DAP_DISPATCH_DEFINE(name)    \
    name##_fn_t name##_ptr = NULL

/**
 * Declare + define a file-local (static) function pointer.
 * For dispatch that stays within a single translation unit.
 */
#define DAP_DISPATCH_LOCAL(name, ret_t, ...)             \
    typedef ret_t (*name##_fn_t)(__VA_ARGS__);           \
    static name##_fn_t name##_ptr = NULL

/**
 * Static inline dispatch with cached function pointer — for headers.
 *
 * Each TU that includes the header gets its own static cache.
 * First call: resolve → cache. Subsequent: one predicted indirect call.
 * The resolve function is generated with platform-specific prediction:
 * on x86 the AVX2 path is predicted taken, on ARM the NEON path.
 *
 * Usage (in header):
 *
 *   DAP_DISPATCH_DECLARE_RESOLVE(my_func, void, int *, size_t);
 *
 *   static inline my_func_fn_t my_func_resolve(void) {
 *       dap_cpu_arch_t arch = dap_cpu_arch_get_best();
 *       DAP_DISPATCH_RESOLVE_X86(DAP_CPU_ARCH_AVX512, my_func_avx512);
 *       DAP_DISPATCH_RESOLVE_X86(DAP_CPU_ARCH_AVX2,   my_func_avx2);
 *       DAP_DISPATCH_RESOLVE_ARM(DAP_CPU_ARCH_NEON,    my_func_neon);
 *       return my_func_ref;
 *   }
 *
 *   static inline void my_func(int *a, size_t n) {
 *       DAP_DISPATCH_INLINE_CALL(my_func, a, n);
 *   }
 */

/**
 * Declare fn_t typedef + static cache pointer for header dispatch.
 * Does NOT create extern storage — each TU has its own cache.
 */
#define DAP_DISPATCH_DECLARE_RESOLVE(name, ret_t, ...)   \
    typedef ret_t (*name##_fn_t)(__VA_ARGS__);           \
    static name##_fn_t name##_cached_ptr = NULL

/**
 * Resolve helpers with platform prediction:
 * On x86 the highest match is predicted taken; on ARM likewise.
 */
#if DAP_PLATFORM_X86
#  define DAP_DISPATCH_RESOLVE_X86(arch_enum, impl)                          \
    if (__builtin_expect(arch >= (arch_enum), 1)) return (impl)
#  define DAP_DISPATCH_RESOLVE_ARM(arch_enum, impl)
#elif DAP_PLATFORM_ARM
#  define DAP_DISPATCH_RESOLVE_X86(arch_enum, impl)
#  define DAP_DISPATCH_RESOLVE_ARM(arch_enum, impl)                          \
    if (__builtin_expect(arch >= (arch_enum), 1)) return (impl)
#else
#  define DAP_DISPATCH_RESOLVE_X86(arch_enum, impl)
#  define DAP_DISPATCH_RESOLVE_ARM(arch_enum, impl)
#endif

/**
 * Inline call through cached pointer. Resolve on first call.
 * Use inside static inline wrapper — becomes one predicted indirect call.
 */
#define DAP_DISPATCH_INLINE_CALL(name, ...)                                  \
    do {                                                                     \
        if (__builtin_expect(!name##_cached_ptr, 0))                         \
            name##_cached_ptr = name##_resolve();                            \
        name##_cached_ptr(__VA_ARGS__);                                      \
    } while (0)

/**
 * Same as DAP_DISPATCH_INLINE_CALL but returns the value.
 */
#define DAP_DISPATCH_INLINE_CALL_RET(name, ...)                              \
    (__builtin_expect(!name##_cached_ptr, 0)                                 \
        ? (name##_cached_ptr = name##_resolve(), name##_cached_ptr(__VA_ARGS__)) \
        : name##_cached_ptr(__VA_ARGS__))

/* ========================================================================== */
/*             Lazy-init guard (for static inline wrappers)                   */
/* ========================================================================== */

/**
 * Ensure dispatch pointer is initialized. If NULL, call init_fn once.
 * Use inside static inline dispatch wrappers, right before ptr(args).
 *
 * After first call: single branch (predicted taken), zero overhead.
 */
#define DAP_DISPATCH_ENSURE(name, init_fn)               \
    if (__builtin_expect(!(name##_ptr), 0))              \
        init_fn()

/* ========================================================================== */
/*                   Init-function helper macros                              */
/* ========================================================================== */

/**
 * Set the default (reference/fallback) implementation.
 */
#define DAP_DISPATCH_DEFAULT(name, impl)   name##_ptr = (impl)

/**
 * Open architecture selection block. Queries best available arch once.
 * Declare as a statement inside init functions.
 */
#define DAP_DISPATCH_ARCH_SELECT                                 \
    dap_cpu_arch_t l_best_arch = dap_cpu_arch_get_best();        \
    (void)l_best_arch

/**
 * Same as DAP_DISPATCH_ARCH_SELECT but considers per-algorithm-class
 * CPU tuning rules (e.g. cap NTT at AVX2 on AMD Zen4 where AVX-512
 * causes frequency throttling).
 *
 * Usage: DAP_DISPATCH_ARCH_SELECT_FOR(my_registered_class);
 */
#define DAP_DISPATCH_ARCH_SELECT_FOR(algo_class)                         \
    dap_cpu_arch_t l_best_arch = dap_cpu_arch_get_best_for(algo_class);  \
    (void)l_best_arch

/**
 * Conditionally upgrade dispatch pointer for an x86 arch level.
 * Call from lowest to highest: AVX2 first, then AVX512.
 * Higher levels overwrite lower (last match wins).
 * Compiles to nothing on non-x86 platforms.
 */
#if DAP_PLATFORM_X86
#  define DAP_DISPATCH_X86(arch_enum, name, impl)                \
    if (l_best_arch >= (arch_enum)) { name##_ptr = (impl); }
#else
#  define DAP_DISPATCH_X86(arch_enum, name, impl)
#endif

/**
 * Conditionally upgrade dispatch pointer for an ARM arch level.
 * Same ordering rule: NEON first, then SVE, then SVE2.
 * Compiles to nothing on non-ARM platforms.
 */
#if DAP_PLATFORM_ARM
#  define DAP_DISPATCH_ARM(arch_enum, name, impl)                \
    if (l_best_arch >= (arch_enum)) { name##_ptr = (impl); }
#else
#  define DAP_DISPATCH_ARM(arch_enum, name, impl)
#endif

#ifdef __cplusplus
}
#endif
