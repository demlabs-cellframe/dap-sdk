/*
 * Integer utility functions: rotation, 128-bit multiply/divide, byte swap.
 *
 * Originally based on Monero/Cryptonote int-util.h; rewritten for DAP SDK.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/param.h>

#if defined(__ANDROID__)
#include <byteswap.h>
#endif

#if defined(__sun) && defined(__SVR4)
#include <endian.h>
#endif

#if defined(_MSC_VER)
#include <stdlib.h>

static inline uint32_t dap_rol32(uint32_t x, int r) {
    static_assert(sizeof(uint32_t) == sizeof(unsigned int), "this code assumes 32-bit integers");
    return _rotl(x, r);
}

static inline uint64_t dap_rol64(uint64_t x, int r) {
    return _rotl64(x, r);
}

#else

static inline uint32_t dap_rol32(uint32_t x, int r) {
    return (x << (r & 31)) | (x >> (-r & 31));
}

static inline uint64_t dap_rol64(uint64_t x, int r) {
    return (x << (r & 63)) | (x >> (-r & 63));
}

#endif

static inline uint64_t dap_hi_dword(uint64_t val) {
    return val >> 32;
}

static inline uint64_t dap_lo_dword(uint64_t val) {
    return val & 0xFFFFFFFF;
}

static inline uint64_t dap_mul128(uint64_t multiplier, uint64_t multiplicand, uint64_t *product_hi) {
    uint64_t a = dap_hi_dword(multiplier);
    uint64_t b = dap_lo_dword(multiplier);
    uint64_t c = dap_hi_dword(multiplicand);
    uint64_t d = dap_lo_dword(multiplicand);

    uint64_t ac = a * c;
    uint64_t ad = a * d;
    uint64_t bc = b * c;
    uint64_t bd = b * d;

    uint64_t adbc = ad + bc;
    uint64_t adbc_carry = adbc < ad ? 1 : 0;

    uint64_t product_lo = bd + (adbc << 32);
    uint64_t product_lo_carry = product_lo < bd ? 1 : 0;
    *product_hi = ac + (adbc >> 32) + (adbc_carry << 32) + product_lo_carry;
    assert(ac <= *product_hi);

    return product_lo;
}

static inline uint64_t dap_div_with_remainder(uint64_t dividend, uint32_t divisor, uint32_t *remainder) {
    dividend |= ((uint64_t)*remainder) << 32;
    *remainder = dividend % divisor;
    return dividend / divisor;
}

static inline uint32_t dap_div128_32(uint64_t dividend_hi, uint64_t dividend_lo,
                                     uint32_t divisor,
                                     uint64_t *quotient_hi, uint64_t *quotient_lo) {
    uint64_t dividend_dwords[4];
    uint32_t remainder = 0;

    dividend_dwords[3] = dap_hi_dword(dividend_hi);
    dividend_dwords[2] = dap_lo_dword(dividend_hi);
    dividend_dwords[1] = dap_hi_dword(dividend_lo);
    dividend_dwords[0] = dap_lo_dword(dividend_lo);

    *quotient_hi  = dap_div_with_remainder(dividend_dwords[3], divisor, &remainder) << 32;
    *quotient_hi |= dap_div_with_remainder(dividend_dwords[2], divisor, &remainder);
    *quotient_lo  = dap_div_with_remainder(dividend_dwords[1], divisor, &remainder) << 32;
    *quotient_lo |= dap_div_with_remainder(dividend_dwords[0], divisor, &remainder);

    return remainder;
}

#define DAP_IDENT32(x) ((uint32_t) (x))
#define DAP_IDENT64(x) ((uint64_t) (x))

#define DAP_SWAP32(x) ((((uint32_t) (x) & 0x000000ff) << 24) | \
    (((uint32_t) (x) & 0x0000ff00) <<  8) | \
    (((uint32_t) (x) & 0x00ff0000) >>  8) | \
    (((uint32_t) (x) & 0xff000000) >> 24))

#define DAP_SWAP64(x) ((((uint64_t) (x) & 0x00000000000000ff) << 56) | \
    (((uint64_t) (x) & 0x000000000000ff00) << 40) | \
    (((uint64_t) (x) & 0x0000000000ff0000) << 24) | \
    (((uint64_t) (x) & 0x00000000ff000000) <<  8) | \
    (((uint64_t) (x) & 0x000000ff00000000) >>  8) | \
    (((uint64_t) (x) & 0x0000ff0000000000) >> 24) | \
    (((uint64_t) (x) & 0x00ff000000000000) >> 40) | \
    (((uint64_t) (x) & 0xff00000000000000) >> 56))

static inline uint32_t dap_ident32(uint32_t x) { return x; }
static inline uint64_t dap_ident64(uint64_t x) { return x; }

#ifndef __OpenBSD__
#  if defined(__ANDROID__) && defined(__swap32) && !defined(dap_swap32)
#      define dap_swap32 __swap32
#  elif !defined(dap_swap32)
static inline uint32_t dap_swap32(uint32_t x) {
    x = ((x & 0x00ff00ff) << 8) | ((x & 0xff00ff00) >> 8);
    return (x << 16) | (x >> 16);
}
#  endif
#  if defined(__ANDROID__) && defined(__swap64) && !defined(dap_swap64)
#      define dap_swap64 __swap64
#  elif !defined(dap_swap64)
static inline uint64_t dap_swap64(uint64_t x) {
    x = ((x & 0x00ff00ff00ff00ff) <<  8) | ((x & 0xff00ff00ff00ff00) >>  8);
    x = ((x & 0x0000ffff0000ffff) << 16) | ((x & 0xffff0000ffff0000) >> 16);
    return (x << 32) | (x >> 32);
}
#  endif
#endif /* __OpenBSD__ */

#if defined(__GNUC__)
#define DAP_INT_UTIL_UNUSED __attribute__((unused))
#else
#define DAP_INT_UTIL_UNUSED
#endif
static inline void dap_mem_inplace_ident(void *mem DAP_INT_UTIL_UNUSED, size_t n DAP_INT_UTIL_UNUSED) { }
#undef DAP_INT_UTIL_UNUSED

static inline void dap_mem_inplace_swap32(void *mem, size_t n) {
    for (size_t i = 0; i < n; i++)
        ((uint32_t *) mem)[i] = dap_swap32(((const uint32_t *) mem)[i]);
}

static inline void dap_mem_inplace_swap64(void *mem, size_t n) {
    for (size_t i = 0; i < n; i++)
        ((uint64_t *) mem)[i] = dap_swap64(((const uint64_t *) mem)[i]);
}

static inline void dap_memcpy_ident32(void *dst, const void *src, size_t n) {
    memcpy(dst, src, 4 * n);
}

static inline void dap_memcpy_ident64(void *dst, const void *src, size_t n) {
    memcpy(dst, src, 8 * n);
}

static inline void dap_memcpy_swap32(void *dst, const void *src, size_t n) {
    for (size_t i = 0; i < n; i++)
        ((uint32_t *) dst)[i] = dap_swap32(((const uint32_t *) src)[i]);
}

static inline void dap_memcpy_swap64(void *dst, const void *src, size_t n) {
    for (size_t i = 0; i < n; i++)
        ((uint64_t *) dst)[i] = dap_swap64(((const uint64_t *) src)[i]);
}

#if !defined(BYTE_ORDER) || !defined(LITTLE_ENDIAN) || !defined(BIG_ENDIAN)
static_assert(false, "BYTE_ORDER is undefined. Perhaps, GNU extensions are not enabled");
#endif

#if BYTE_ORDER == LITTLE_ENDIAN
#define DAP_SWAP32LE DAP_IDENT32
#define DAP_SWAP32BE DAP_SWAP32
#define dap_swap32le dap_ident32
#define dap_swap32be dap_swap32
#define dap_mem_inplace_swap32le dap_mem_inplace_ident
#define dap_mem_inplace_swap32be dap_mem_inplace_swap32
#define dap_memcpy_swap32le dap_memcpy_ident32
#define dap_memcpy_swap32be dap_memcpy_swap32
#define DAP_SWAP64LE DAP_IDENT64
#define DAP_SWAP64BE DAP_SWAP64
#define dap_swap64le dap_ident64
#define dap_swap64be dap_swap64
#define dap_mem_inplace_swap64le dap_mem_inplace_ident
#define dap_mem_inplace_swap64be dap_mem_inplace_swap64
#define dap_memcpy_swap64le dap_memcpy_ident64
#define dap_memcpy_swap64be dap_memcpy_swap64
#endif

#if BYTE_ORDER == BIG_ENDIAN
#define DAP_SWAP32BE DAP_IDENT32
#define DAP_SWAP32LE DAP_SWAP32
#define dap_swap32be dap_ident32
#define dap_swap32le dap_swap32
#define dap_mem_inplace_swap32be dap_mem_inplace_ident
#define dap_mem_inplace_swap32le dap_mem_inplace_swap32
#define dap_memcpy_swap32be dap_memcpy_ident32
#define dap_memcpy_swap32le dap_memcpy_swap32
#define DAP_SWAP64BE DAP_IDENT64
#define DAP_SWAP64LE DAP_SWAP64
#define dap_swap64be dap_ident64
#define dap_swap64le dap_swap64
#define dap_mem_inplace_swap64be dap_mem_inplace_ident
#define dap_mem_inplace_swap64le dap_mem_inplace_swap64
#define dap_memcpy_swap64be dap_memcpy_ident64
#define dap_memcpy_swap64le dap_memcpy_swap64
#endif
