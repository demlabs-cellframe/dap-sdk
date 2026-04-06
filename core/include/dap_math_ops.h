#pragma once
#include <stdint.h>
#include "assert.h"
#include "signal.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <inttypes.h>

#include "dap_common.h"

#if defined(__GNUC__) || defined (__clang__)

#if __SIZEOF_INT128__ == 16

#define DAP_GLOBAL_IS_INT128
typedef __int128 _dap_int128_t;

#if !defined (int128_t)
typedef __int128 int128_t;
#endif

#if !defined (uint128_t)
typedef unsigned __int128 uint128_t;
#endif

#else // __SIZEOF_INT128__ == 16

typedef union uint128 {
    struct{
         uint64_t lo;
         uint64_t hi;
    } DAP_ALIGN_PACKED;
    struct{
        uint32_t c;
        uint32_t d;
        uint32_t a;
        uint32_t b;
    } DAP_ALIGN_PACKED u32;
} uint128_t;


typedef union int128 {
    int64_t i64[2];
    int32_t i32[4];
} int128_t;

typedef int128_t _dap_int128_t;

#endif // __SIZEOF_INT128__ == 16

typedef struct uint256_t {
    union {
        struct {
            uint128_t hi;
            uint128_t lo;
        } DAP_ALIGN_PACKED;
        struct {
            struct {
                uint64_t a;
                uint64_t b;
            } DAP_ALIGN_PACKED _hi;
            struct {
                uint64_t a;
                uint64_t b;
            } DAP_ALIGN_PACKED _lo;
        } DAP_ALIGN_PACKED;
        struct {
            struct {
                uint32_t c;
                uint32_t d;
                uint32_t a;
                uint32_t b;
            } DAP_ALIGN_PACKED __hi;
            struct {
                uint32_t c;
                uint32_t d;
                uint32_t a;
                uint32_t b;
            }DAP_ALIGN_PACKED __lo;
        } DAP_ALIGN_PACKED;
    } DAP_ALIGN_PACKED;
} DAP_ALIGN_PACKED uint256_t;

typedef struct uint512_t {
    uint256_t hi;
    uint256_t lo;
} DAP_ALIGN_PACKED  uint512_t;

#endif //defined(__GNUC__) || defined (__clang__)

#define lo_32 ((uint64_t)0xffffffff)
#define hi_32 ((uint64_t)0xffffffff00000000)
#define ones_64 ((uint64_t)0xffffffffffffffff)

////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

extern const uint128_t uint128_0;
extern const uint128_t uint128_1;
extern const uint128_t uint128_max;
extern const uint256_t uint256_0;
extern const uint256_t uint256_1;
extern const uint256_t uint256_max;

extern const uint512_t uint512_0;

/* ------------------------------------------------------------------------- */
/* Pack integers into uint128 / uint256                                       */
/* ------------------------------------------------------------------------- */

static inline uint128_t GET_128_FROM_64(uint64_t n) {
#ifdef DAP_GLOBAL_IS_INT128
    return (uint128_t) n;
#else
    return (uint128_t) {{ .lo = n, .hi = 0 }};
#endif
}

static inline uint128_t GET_128_FROM_64_64(uint64_t hi, uint64_t lo) {
#ifdef DAP_GLOBAL_IS_INT128
    return (uint128_t)lo + ((uint128_t)hi << 64);
#else
    return (uint128_t) {{ .lo = lo, .hi = hi }};
#endif
}

static inline uint256_t GET_256_FROM_64(uint64_t n) {
    return (uint256_t) {{{ .hi = uint128_0, .lo = GET_128_FROM_64(n) }}};
}

static inline uint256_t GET_256_FROM_128(uint128_t n) {
    return (uint256_t) {{{ .hi = uint128_0, .lo = n }}};
}

/* ------------------------------------------------------------------------- */
/* Equality / zero tests                                                      */
/* ------------------------------------------------------------------------- */

static inline bool EQUAL_128(uint128_t a_128_bit, uint128_t b_128_bit){
#ifdef DAP_GLOBAL_IS_INT128
    return a_128_bit == b_128_bit;
#else
    return a_128_bit.lo==b_128_bit.lo && a_128_bit.hi==b_128_bit.hi;
#endif
}

static inline bool IS_ZERO_128(uint128_t a_128_bit){
    return EQUAL_128(a_128_bit, uint128_0);
}

static inline bool EQUAL_256(uint256_t a_256_bit, uint256_t b_256_bit){

#ifdef DAP_GLOBAL_IS_INT128
    return a_256_bit.lo==b_256_bit.lo && a_256_bit.hi==b_256_bit.hi;

#else
    return a_256_bit.lo.lo==b_256_bit.lo.lo &&
           a_256_bit.lo.hi==b_256_bit.lo.hi &&
           a_256_bit.hi.lo==b_256_bit.hi.lo &&
           a_256_bit.hi.hi==b_256_bit.hi.hi;
#endif
}

static inline bool IS_ZERO_256(uint256_t a_256_bit){
#ifdef DAP_GLOBAL_IS_INT128
    return !a_256_bit.hi && !a_256_bit.lo;
#else
    return !(a_256_bit.hi.hi | a_256_bit.hi.lo | a_256_bit.lo.hi | a_256_bit.lo.lo);
#endif
}

/* ------------------------------------------------------------------------- */
/* Compare (order: > / == / <  ->  1 / 0 / -1)                                */
/* ------------------------------------------------------------------------- */

static inline int compare128(uint128_t a, uint128_t b)
{
#ifdef DAP_GLOBAL_IS_INT128
    return (a > b) ? 1 : (a < b) ? -1 : 0;
#else
    return (a.hi > b.hi) ? 1 : (a.hi < b.hi) ? -1 : (a.lo > b.lo) ? 1 : (a.lo < b.lo) ? -1 : 0;
#endif
}

static inline int compare256(uint256_t a, uint256_t b)
{
#ifdef DAP_GLOBAL_IS_INT128
    int c = (a.hi > b.hi) ? 1 : (a.hi < b.hi) ? -1 : 0;
    return c ? c : ((a.lo > b.lo) ? 1 : (a.lo < b.lo) ? -1 : 0);
#else
    int c = compare128(a.hi, b.hi);
    return c ? c : compare128(a.lo, b.lo);
#endif
}

static inline int compare256_ptr(uint256_t *a, uint256_t *b) { return compare256(*a, *b); }

/* ------------------------------------------------------------------------- */
/* Bitwise AND / OR                                                           */
/* ------------------------------------------------------------------------- */

static inline uint128_t AND_128(uint128_t a_128_bit,uint128_t b_128_bit){

#ifdef DAP_GLOBAL_IS_INT128
    return a_128_bit&b_128_bit;
#else
    uint128_t output=uint128_0;
    output.hi= a_128_bit.hi & b_128_bit.hi;
    output.lo= a_128_bit.lo & b_128_bit.lo;
    return output;
#endif
}

static inline uint128_t OR_128(uint128_t a_128_bit,uint128_t b_128_bit){

#ifdef DAP_GLOBAL_IS_INT128
    return a_128_bit|b_128_bit;

#else
    uint128_t output=uint128_0;
    output.hi= a_128_bit.hi | b_128_bit.hi;
    output.lo= a_128_bit.lo | b_128_bit.lo;
    return output;
#endif
}

static inline uint256_t AND_256(uint256_t a_256_bit,uint256_t b_256_bit){
    return (uint256_t) {{{
        .hi = AND_128(a_256_bit.hi, b_256_bit.hi),
        .lo = AND_128(a_256_bit.lo, b_256_bit.lo)
    }}};
}

static inline uint256_t OR_256(uint256_t a_256_bit,uint256_t b_256_bit){
    return (uint256_t) {{{
        .hi = OR_128(a_256_bit.hi, b_256_bit.hi),
        .lo = OR_128(a_256_bit.lo, b_256_bit.lo)
    }}};
}

/* ------------------------------------------------------------------------- */
/* Shifts                                                                     */
/* ------------------------------------------------------------------------- */

static inline void LEFT_SHIFT_128(uint128_t a_128_bit,uint128_t* b_128_bit,int n){
    assert (n <= 128);

#ifdef DAP_GLOBAL_IS_INT128
    *b_128_bit= a_128_bit << n;

#else
    if (n >= 64) {
        b_128_bit->hi = (n == 64) ? a_128_bit.lo : (a_128_bit.lo << (n - 64));
        b_128_bit->lo = 0;
    } else if (n == 0) {
        *b_128_bit = a_128_bit;
    } else {
        b_128_bit->hi = (a_128_bit.hi << n) | (a_128_bit.lo >> (64 - n));
        b_128_bit->lo = a_128_bit.lo << n;
    }

#endif
}

static inline void RIGHT_SHIFT_128(uint128_t a_128_bit,uint128_t* b_128_bit,int n){
    assert (n <= 128);

#ifdef DAP_GLOBAL_IS_INT128
    (*b_128_bit) = a_128_bit >> n;
#else
    if (n >= 64) {
        b_128_bit->lo = (n == 64) ? a_128_bit.hi : (a_128_bit.hi >> (n - 64));
        b_128_bit->hi = 0;
    } else if (n == 0) {
        *b_128_bit = a_128_bit;
    } else {
        b_128_bit->lo = (a_128_bit.lo >> n) | (a_128_bit.hi << (64 - n));
        b_128_bit->hi = a_128_bit.hi >> n;
    }
#endif
}

static inline void LEFT_SHIFT_256(uint256_t a_256_bit, uint256_t *b_256_bit, int n){
    assert(n <= 256);
    if (n >= 128) {
        uint128_t hi_out;
        LEFT_SHIFT_128(a_256_bit.lo, &hi_out, n - 128);
        *b_256_bit = (uint256_t){{{ .hi = hi_out, .lo = uint128_0 }}};
    } else if (n == 0) {
        *b_256_bit = a_256_bit;
    } else {
        uint128_t hi_out, lo_out, carry;
        LEFT_SHIFT_128(a_256_bit.hi, &hi_out, n);
        RIGHT_SHIFT_128(a_256_bit.lo, &carry, 128 - n);
        hi_out = OR_128(hi_out, carry);
        LEFT_SHIFT_128(a_256_bit.lo, &lo_out, n);
        *b_256_bit = (uint256_t){{{ .hi = hi_out, .lo = lo_out }}};
    }
}

static inline void RIGHT_SHIFT_256(uint256_t a_256_bit, uint256_t *b_256_bit, int n){
    assert(n <= 256);
    if (n >= 128) {
        uint128_t lo_out;
        RIGHT_SHIFT_128(a_256_bit.hi, &lo_out, n - 128);
        *b_256_bit = (uint256_t){{{ .hi = uint128_0, .lo = lo_out }}};
    } else if (n == 0) {
        *b_256_bit = a_256_bit;
    } else {
        uint128_t lo_out, hi_out, carry;
        RIGHT_SHIFT_128(a_256_bit.lo, &lo_out, n);
        LEFT_SHIFT_128(a_256_bit.hi, &carry, 128 - n);
        lo_out = OR_128(lo_out, carry);
        RIGHT_SHIFT_128(a_256_bit.hi, &hi_out, n);
        *b_256_bit = (uint256_t){{{ .hi = hi_out, .lo = lo_out }}};
    }
}

/* ------------------------------------------------------------------------- */
/* Increment / decrement                                                      */
/* ------------------------------------------------------------------------- */

static inline void INCR_128(uint128_t *a_128_bit){

#ifdef DAP_GLOBAL_IS_INT128
    (*a_128_bit)++;

#else
    a_128_bit->lo++;
    if(a_128_bit->lo == 0)
    {
        a_128_bit->hi++;
    }
#endif
}

static inline void DECR_128(uint128_t* a_128_bit){

#ifdef DAP_GLOBAL_IS_INT128
    (*a_128_bit)--;

#else
    if (a_128_bit->lo == 0) {
        a_128_bit->hi--;
    }
    a_128_bit->lo--;

#endif
}

static inline void INCR_256(uint256_t* a_256_bit){

#ifdef DAP_GLOBAL_IS_INT128
    a_256_bit->lo++;
    if(a_256_bit->lo == 0)
    {
        a_256_bit->hi++;
    }

#else
    INCR_128(&a_256_bit->lo);
    if(EQUAL_128(a_256_bit->lo, uint128_0))
    {
        INCR_128(&a_256_bit->hi);
    }
#endif
}

static inline void DECR_256(uint256_t* a_256_bit) {
#ifdef DAP_GLOBAL_IS_INT128
    if (a_256_bit->lo == 0) {
        a_256_bit->hi--;
    }
    a_256_bit->lo--;
#else
    if(EQUAL_128(a_256_bit->lo, uint128_0)) {
        DECR_128(&a_256_bit->hi);
    }
    DECR_128(&a_256_bit->lo);
#endif
}

/* ------------------------------------------------------------------------- */
/* Add / subtract (64 .. 512)                                                 */
/* ------------------------------------------------------------------------- */

static inline int SUM_64_64(uint64_t a_64_bit,uint64_t b_64_bit,uint64_t* c_64_bit )
{
    return *c_64_bit = a_64_bit + b_64_bit, (int)(*c_64_bit < a_64_bit);
}

static inline int OVERFLOW_SUM_64_64(uint64_t a_64_bit,uint64_t b_64_bit)
{
    return (int)(a_64_bit + b_64_bit < a_64_bit);
}

static inline int OVERFLOW_MULT_64_64(uint64_t a_64_bit,uint64_t b_64_bit)
{
    return (int)(b_64_bit && a_64_bit > ((uint64_t)-1) / b_64_bit);
}

static inline int MULT_64_64(uint64_t a_64_bit,uint64_t b_64_bit,uint64_t* c_64_bit ) {
    return *c_64_bit = a_64_bit * b_64_bit, OVERFLOW_MULT_64_64(a_64_bit, b_64_bit);
}

//Mixed precision: add a uint64_t into a uint128_t
static inline int ADD_64_INTO_128(uint64_t a_64_bit,uint128_t *c_128_bit )
{
    int overflow_flag=0;
#ifdef DAP_GLOBAL_IS_INT128
    uint128_t temp=*c_128_bit;
    *c_128_bit+=(uint128_t)a_64_bit;
    overflow_flag=(*c_128_bit<temp);
#else
    uint64_t overflow_64 = 0;
    uint64_t temp = 0;
    overflow_flag = SUM_64_64(a_64_bit, c_128_bit->lo, &temp);
    overflow_64 = overflow_flag;
    c_128_bit->lo = temp;
    overflow_flag = SUM_64_64(overflow_64, c_128_bit->hi, &temp);
    c_128_bit->hi = temp;
#endif
    return overflow_flag;
}

static inline int SUM_128_128(uint128_t a_128_bit,uint128_t b_128_bit,uint128_t* c_128_bit)
{
    int overflow_flag=0;
#ifdef DAP_GLOBAL_IS_INT128
    *c_128_bit=a_128_bit+b_128_bit;
    overflow_flag=(*c_128_bit<a_128_bit);
    return overflow_flag;
#else
    int overflow_flag_intermediate;
    uint64_t temp = 0;
    overflow_flag = SUM_64_64(a_128_bit.lo, b_128_bit.lo, &temp);
    c_128_bit->lo = temp;
    uint64_t carry_in_64=overflow_flag;
    uint64_t intermediate_value=0;
    overflow_flag=0;
    overflow_flag=SUM_64_64(carry_in_64,a_128_bit.hi,&intermediate_value);
    overflow_flag_intermediate = SUM_64_64(intermediate_value, b_128_bit.hi, &temp);
    c_128_bit->hi = temp;
    int return_overflow=overflow_flag|overflow_flag_intermediate;
    return return_overflow;
#endif
}

static inline int SUBTRACT_128_128(uint128_t a_128_bit, uint128_t b_128_bit, uint128_t* c_128_bit)
{
    int underflow_flag = 0;
#ifdef DAP_GLOBAL_IS_INT128
    *c_128_bit = a_128_bit - b_128_bit;
    underflow_flag = a_128_bit < b_128_bit;
#else
    c_128_bit->lo = a_128_bit.lo - b_128_bit.lo;
    int borrow = (a_128_bit.lo < b_128_bit.lo);
    c_128_bit->hi = a_128_bit.hi - b_128_bit.hi - borrow;
    underflow_flag = (a_128_bit.hi < b_128_bit.hi) || (a_128_bit.hi == b_128_bit.hi && borrow);
#endif
    return underflow_flag;
}

//Mixed precision: add a uint128_t into a uint256_t
static inline int ADD_128_INTO_256(uint128_t a_128_bit,uint256_t* c_256_bit) {
    int overflow_flag=0;
    uint128_t overflow_128 = uint128_0, temp = uint128_0;
    overflow_flag=SUM_128_128(a_128_bit, c_256_bit->lo, &temp);
    c_256_bit->lo = temp;

#ifdef DAP_GLOBAL_IS_INT128
    overflow_128=overflow_flag;
#else
    overflow_128.lo=overflow_flag;
#endif

    overflow_flag=SUM_128_128(overflow_128, c_256_bit->hi, &temp);
    c_256_bit->hi = temp;
    return overflow_flag;
}

/*
 * SUM_256_256 -- unsigned 256 + 256 -> 256 bits.
 *
 * Returns 1 if the sum does not fit in 256 bits (unsigned overflow), else 0.
 * The low 256 bits of the sum are stored in *c_256_bit.
 *
 * x86-64 (GCC/Clang): one addq/adcq chain over four 64-bit limbs (least significant first).
 * Overflow flag via sbbq/negq: after the final adcq, CF==1 iff there was a carry out of the
 * top limb (sum wider than 256 bits). Tighter and more scheduler-friendly than re-deriving
 * overflow from C compares on the widened result.
 *
 * AArch64: adds/adcs over four limbs; cset %0,cs sets `of` to 1 if C is set after the last adcs
 * (same meaning: carry out of the MS limb).
 *
 * Other targets with __int128: add lo halves, then hi halves with a uint128 carry-in -- portable
 * when no target-specific asm is used.
 *
 * Without __int128: two SUM_128_128 steps on the half-words, propagating carry into the high half.
 */
static inline int SUM_256_256(uint256_t a_256_bit,uint256_t b_256_bit,uint256_t* c_256_bit)
{
#ifdef DAP_GLOBAL_IS_INT128
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    uint64_t a0 = (uint64_t)a_256_bit.lo, a1 = (uint64_t)(a_256_bit.lo >> 64);
    uint64_t a2 = (uint64_t)a_256_bit.hi, a3 = (uint64_t)(a_256_bit.hi >> 64);
    uint64_t of;
    __asm__("addq %5,%1\n\t" "adcq %6,%2\n\t" "adcq %7,%3\n\t" "adcq %8,%4\n\t"
            "sbbq %0,%0\n\t" "negq %0"
        : "=&r"(of), "+&r"(a0), "+&r"(a1), "+&r"(a2), "+&r"(a3)
        : "r"((uint64_t)b_256_bit.lo), "r"((uint64_t)(b_256_bit.lo >> 64)),
          "r"((uint64_t)b_256_bit.hi), "r"((uint64_t)(b_256_bit.hi >> 64))
        : "cc");
    c_256_bit->lo = (uint128_t)a0 | ((uint128_t)a1 << 64);
    c_256_bit->hi = (uint128_t)a2 | ((uint128_t)a3 << 64);
    return (int)of;
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    uint64_t a0 = (uint64_t)a_256_bit.lo, a1 = (uint64_t)(a_256_bit.lo >> 64);
    uint64_t a2 = (uint64_t)a_256_bit.hi, a3 = (uint64_t)(a_256_bit.hi >> 64);
    uint64_t of;
    __asm__("adds %1,%1,%5\n\t" "adcs %2,%2,%6\n\t" "adcs %3,%3,%7\n\t" "adcs %4,%4,%8\n\t"
            "cset %0,cs"
        : "=&r"(of), "+&r"(a0), "+&r"(a1), "+&r"(a2), "+&r"(a3)
        : "r"((uint64_t)b_256_bit.lo), "r"((uint64_t)(b_256_bit.lo >> 64)),
          "r"((uint64_t)b_256_bit.hi), "r"((uint64_t)(b_256_bit.hi >> 64))
        : "cc");
    c_256_bit->lo = (uint128_t)a0 | ((uint128_t)a1 << 64);
    c_256_bit->hi = (uint128_t)a2 | ((uint128_t)a3 << 64);
    return (int)of;
#else
    c_256_bit->lo = a_256_bit.lo + b_256_bit.lo;
    uint128_t carry = (c_256_bit->lo < a_256_bit.lo);
    uint128_t sum_hi = a_256_bit.hi + b_256_bit.hi;
    c_256_bit->hi = sum_hi + carry;
    return (sum_hi < a_256_bit.hi) | (c_256_bit->hi < sum_hi);
#endif
#else
    uint128_t tmp;
    int of1 = SUM_128_128(a_256_bit.lo, b_256_bit.lo, &c_256_bit->lo);
    uint128_t carry_in_128;
    carry_in_128.hi = 0;
    carry_in_128.lo = of1;
    int of2 = SUM_128_128(carry_in_128, a_256_bit.hi, &tmp);
    int of3 = SUM_128_128(tmp, b_256_bit.hi, &c_256_bit->hi);
    return of2 | of3;
#endif
}

/*
 * SUBTRACT_256_256 -- unsigned a - b -> 256 bits.
 *
 * Returns 1 if a < b (underflow in the unsigned sense), else 0. *c_256_bit always receives
 * the 256-bit difference modulo 2^256.
 *
 * x86-64: subq/sbbq across four limbs; the same sbbq/negq epilogue yields uf==1 when the final
 * limb needed a borrow (correct full 256-bit ordering of a vs b, not borrow-from-lo only).
 *
 * AArch64: subs/sbcs; cset %0,cc -- "carry clear" after the chain means a borrow out of the
 * top limb, i.e. a < b as unsigned 256-bit integers.
 *
 * Using only the low-limb borrow as the underflow flag would wrongly return 0 when a.hi < b.hi but
 * the lo halves subtract without borrow; these asm and C paths use the full 256-bit borrow-out.
 */
static inline int SUBTRACT_256_256(uint256_t a_256_bit,uint256_t b_256_bit,uint256_t* c_256_bit)
{
#ifdef DAP_GLOBAL_IS_INT128
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    uint64_t a0 = (uint64_t)a_256_bit.lo, a1 = (uint64_t)(a_256_bit.lo >> 64);
    uint64_t a2 = (uint64_t)a_256_bit.hi, a3 = (uint64_t)(a_256_bit.hi >> 64);
    uint64_t uf;
    __asm__("subq %5,%1\n\t" "sbbq %6,%2\n\t" "sbbq %7,%3\n\t" "sbbq %8,%4\n\t"
            "sbbq %0,%0\n\t" "negq %0"
        : "=&r"(uf), "+&r"(a0), "+&r"(a1), "+&r"(a2), "+&r"(a3)
        : "r"((uint64_t)b_256_bit.lo), "r"((uint64_t)(b_256_bit.lo >> 64)),
          "r"((uint64_t)b_256_bit.hi), "r"((uint64_t)(b_256_bit.hi >> 64))
        : "cc");
    c_256_bit->lo = (uint128_t)a0 | ((uint128_t)a1 << 64);
    c_256_bit->hi = (uint128_t)a2 | ((uint128_t)a3 << 64);
    return (int)uf;
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    uint64_t a0 = (uint64_t)a_256_bit.lo, a1 = (uint64_t)(a_256_bit.lo >> 64);
    uint64_t a2 = (uint64_t)a_256_bit.hi, a3 = (uint64_t)(a_256_bit.hi >> 64);
    uint64_t uf;
    __asm__("subs %1,%1,%5\n\t" "sbcs %2,%2,%6\n\t" "sbcs %3,%3,%7\n\t" "sbcs %4,%4,%8\n\t"
            "cset %0,cc"
        : "=&r"(uf), "+&r"(a0), "+&r"(a1), "+&r"(a2), "+&r"(a3)
        : "r"((uint64_t)b_256_bit.lo), "r"((uint64_t)(b_256_bit.lo >> 64)),
          "r"((uint64_t)b_256_bit.hi), "r"((uint64_t)(b_256_bit.hi >> 64))
        : "cc");
    c_256_bit->lo = (uint128_t)a0 | ((uint128_t)a1 << 64);
    c_256_bit->hi = (uint128_t)a2 | ((uint128_t)a3 << 64);
    return (int)uf;
#else
    c_256_bit->lo = a_256_bit.lo - b_256_bit.lo;
    int borrow = (a_256_bit.lo < b_256_bit.lo);
    c_256_bit->hi = a_256_bit.hi - b_256_bit.hi - borrow;
    return (a_256_bit.hi < b_256_bit.hi) || (a_256_bit.hi == b_256_bit.hi && borrow);
#endif
#else
    uint64_t t, r, borrow;

    t = a_256_bit.lo.lo;
    r = t - b_256_bit.lo.lo;
    borrow = (r > t);
    c_256_bit->lo.lo = r;

    t = a_256_bit.lo.hi;
    t -= borrow;
    borrow = (t > a_256_bit.lo.hi);
    r = t - b_256_bit.lo.hi;
    borrow |= (r > t);
    c_256_bit->lo.hi = r;

    t = a_256_bit.hi.lo;
    t -= borrow;
    borrow = (t > a_256_bit.hi.lo);
    r = t - b_256_bit.hi.lo;
    borrow |= (r > t);
    c_256_bit->hi.lo = r;

    t = a_256_bit.hi.hi;
    t -= borrow;
    borrow = (t > a_256_bit.hi.hi);
    r = t - b_256_bit.hi.hi;
    borrow |= (r > t);
    c_256_bit->hi.hi = r;

    return borrow;
#endif
}

//Mixed precision: add a uint256_t into a uint512_t
static inline int ADD_256_INTO_512(uint256_t a_256_bit,uint512_t* c_512_bit) {
    int overflow_flag=0;
    uint256_t overflow_256=uint256_0;
    uint256_t temp=uint256_0;
    temp=c_512_bit->lo;
    overflow_flag=SUM_256_256(a_256_bit,temp,&c_512_bit->lo);
#ifdef DAP_GLOBAL_IS_INT128
    overflow_256.lo=overflow_flag;
#else
    overflow_256.lo.lo=overflow_flag;
#endif
    temp=c_512_bit->hi;
    overflow_flag=SUM_256_256(overflow_256,temp,&c_512_bit->hi);
    return overflow_flag;
}

/* ------------------------------------------------------------------------- */
/* Multiply                                                                   */
/* ------------------------------------------------------------------------- */
/*
 * 128x128->256 (MULT_128_256) is on the hot path for MULT_256_512 and COIN. On x86-64 and AArch64
 * under GCC/Clang we use explicit asm: a single large uint128_t expression often lowers to the same
 * four 64x64->128 multiplies but with a longer carry-dependent chain between them.
 * Manual scheduling: two independent "corner" muls (a0*b0 and a1*b1), then two cross terms with a
 * short add/adc chain -- better instruction-level parallelism on out-of-order cores.
 * Without target asm: schoolbook on uint128_t using 64-bit halves; without __int128: MULT_64_128
 * and explicit carry handling.
 */

static inline void MULT_64_128(uint64_t a_64_bit, uint64_t b_64_bit, uint128_t* c_128_bit)
{
#ifdef DAP_GLOBAL_IS_INT128
    *c_128_bit = (uint128_t)a_64_bit * (uint128_t)b_64_bit;
#else
    uint64_t a_64_bit_hi = (a_64_bit & 0xffffffff);
    uint64_t b_64_bit_hi = (b_64_bit & 0xffffffff);
    uint64_t prod_hi = (a_64_bit_hi * b_64_bit_hi);
    uint64_t w3 = (prod_hi & 0xffffffff);
    uint64_t prod_hi_shift_right = (prod_hi >> 32);

    a_64_bit >>= 32;
    prod_hi = (a_64_bit * b_64_bit_hi) + prod_hi_shift_right;
    prod_hi_shift_right = (prod_hi & 0xffffffff);
    uint64_t w1 = (prod_hi >> 32);

    b_64_bit >>= 32;
    prod_hi = (a_64_bit_hi * b_64_bit) + prod_hi_shift_right;
    prod_hi_shift_right = (prod_hi >> 32);

    c_128_bit->hi = (a_64_bit * b_64_bit) + w1 + prod_hi_shift_right;
    c_128_bit->lo = (prod_hi << 32) + w3;
#endif
}

/*
 * MULT_128_256 -- full unsigned 128x128 product (256 bits in *c_256_bit).
 * Decomposition: a = a1*2^64 + a0, b = b1*2^64 + b0; schoolbook multiply on 64-bit half-limbs.
 *
 * x86-64: mulq implicitly uses %rax and writes the 128-bit product to %rax:%rdx.
 *  - First two mulq: a0*b0 -> r0:r1, a1*b1 -> r2:r3 -- distinct operand sets so the core can
 *    issue the second multiply while the first retires (plus cheap movs into named outputs).
 *  - Third and fourth mulq: a0*b1 and a1*b0; partial sums fold into r1,r2,r3 via addq/adcq/adcq --
 *    only three adc-class ops per cross term; carries are not spread across extra temps.
 *  "rm" on b0/b1 allows a memory operand where the compiler chooses (mulq allows it).
 *
 * AArch64: mul is the low 64 bits of the product, umulh the high 64. Same idea: two corners as
 * independent mul/umulh pairs, then two cross terms with adds/adcs/adc into r1..r3.
 * adc ..., xzr adds zero with carry (same role as adc $0 on x86).
 */
static inline void MULT_128_256(uint128_t a_128_bit,uint128_t b_128_bit,uint256_t* c_256_bit ) {
#ifdef DAP_GLOBAL_IS_INT128
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    uint64_t a0 = (uint64_t)a_128_bit, a1 = (uint64_t)(a_128_bit >> 64);
    uint64_t b0 = (uint64_t)b_128_bit, b1 = (uint64_t)(b_128_bit >> 64);
    uint64_t r0, r1, r2, r3;
    __asm__("movq %[a0], %%rax\n\t"
            "mulq %[b0]\n\t"
            "movq %%rax, %[r0]\n\t"
            "movq %%rdx, %[r1]\n\t"
            "movq %[a1], %%rax\n\t"
            "mulq %[b1]\n\t"
            "movq %%rax, %[r2]\n\t"
            "movq %%rdx, %[r3]\n\t"
            "movq %[a0], %%rax\n\t"
            "mulq %[b1]\n\t"
            "addq %%rax, %[r1]\n\t"
            "adcq %%rdx, %[r2]\n\t"
            "adcq $0, %[r3]\n\t"
            "movq %[a1], %%rax\n\t"
            "mulq %[b0]\n\t"
            "addq %%rax, %[r1]\n\t"
            "adcq %%rdx, %[r2]\n\t"
            "adcq $0, %[r3]"
        : [r0] "=&r"(r0), [r1] "=&r"(r1), [r2] "=&r"(r2), [r3] "=&r"(r3)
        : [a0] "r"(a0), [a1] "r"(a1), [b0] "rm"(b0), [b1] "rm"(b1)
        : "rax", "rdx", "cc");
    c_256_bit->lo = (uint128_t)r0 | ((uint128_t)r1 << 64);
    c_256_bit->hi = (uint128_t)r2 | ((uint128_t)r3 << 64);
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
    uint64_t a0 = (uint64_t)a_128_bit, a1 = (uint64_t)(a_128_bit >> 64);
    uint64_t b0 = (uint64_t)b_128_bit, b1 = (uint64_t)(b_128_bit >> 64);
    uint64_t r0, r1, r2, r3, t0, t1;
    __asm__("mul   %[r0], %[a0], %[b0]\n\t"
            "umulh %[r1], %[a0], %[b0]\n\t"
            "mul   %[t0], %[a1], %[b1]\n\t"
            "umulh %[r3], %[a1], %[b1]\n\t"
            "mul   %[t1], %[a0], %[b1]\n\t"
            "adds  %[r1], %[r1], %[t1]\n\t"
            "umulh %[t1], %[a0], %[b1]\n\t"
            "adcs  %[r2], %[t0], %[t1]\n\t"
            "adc   %[r3], %[r3], xzr\n\t"
            "mul   %[t0], %[a1], %[b0]\n\t"
            "adds  %[r1], %[r1], %[t0]\n\t"
            "umulh %[t0], %[a1], %[b0]\n\t"
            "adcs  %[r2], %[r2], %[t0]\n\t"
            "adc   %[r3], %[r3], xzr"
        : [r0] "=&r"(r0), [r1] "=&r"(r1), [r2] "=&r"(r2), [r3] "=&r"(r3),
          [t0] "=&r"(t0), [t1] "=&r"(t1)
        : [a0] "r"(a0), [a1] "r"(a1), [b0] "r"(b0), [b1] "r"(b1)
        : "cc");
    c_256_bit->lo = (uint128_t)r0 | ((uint128_t)r1 << 64);
    c_256_bit->hi = (uint128_t)r2 | ((uint128_t)r3 << 64);
#else
    uint128_t a_128_bit_hi = (a_128_bit & 0xffffffffffffffff);
    uint128_t b_128_bit_hi = (b_128_bit & 0xffffffffffffffff);
    uint128_t prod_hi = (a_128_bit_hi * b_128_bit_hi);
    uint128_t w3 = (prod_hi & 0xffffffffffffffff);
    uint128_t prod_hi_shift_right = (prod_hi >> 64);

    a_128_bit >>= 64;
    prod_hi = (a_128_bit * b_128_bit_hi) + prod_hi_shift_right;
    prod_hi_shift_right = (prod_hi & 0xffffffffffffffff);
    uint64_t w1 = (prod_hi >> 64);

    b_128_bit >>= 64;
    prod_hi = (a_128_bit_hi * b_128_bit) + prod_hi_shift_right;
    prod_hi_shift_right = (prod_hi >> 64);

    c_256_bit->hi = (a_128_bit * b_128_bit) + w1 + prod_hi_shift_right;
    c_256_bit->lo = (prod_hi << 64) + w3;
#endif
#else
    //product of .hi terms - stored in .hi field of c_256_bit
    MULT_64_128(a_128_bit.hi,b_128_bit.hi, &c_256_bit->hi);

    //product of .lo terms - stored in .lo field of c_256_bit
    MULT_64_128(a_128_bit.lo,b_128_bit.lo, &c_256_bit->lo);

    uint128_t cross_product_one = GET_128_FROM_64(0);
    uint128_t cross_product_two = GET_128_FROM_64(0);
    MULT_64_128(a_128_bit.hi, b_128_bit.lo, &cross_product_one);
    c_256_bit->lo.hi += cross_product_one.lo;
    if(c_256_bit->lo.hi < cross_product_one.lo)  // if overflow
    {
        INCR_128(&c_256_bit->hi);
    }
    c_256_bit->hi.lo += cross_product_one.hi;
    if(c_256_bit->hi.lo < cross_product_one.hi)  // if  overflowed
    {
        c_256_bit->hi.hi+=1;
    }

    MULT_64_128(a_128_bit.lo, b_128_bit.hi, &cross_product_two);
    c_256_bit->lo.hi += cross_product_two.lo;
    if(c_256_bit->lo.hi < cross_product_two.lo)  // if overflowed
    {
        INCR_128(&c_256_bit->hi);
    }
    c_256_bit->hi.lo += cross_product_two.hi;
    if(c_256_bit->hi.lo < cross_product_two.hi)  //  overflowed
    {
        c_256_bit->hi.hi+=1;
    }
#endif
}

/*
 * MULT_128_128 -- low 128 bits of the product; returns 1 if the full product exceeds 2^128-1.
 * Overflow test with __int128: compare against UINT128_MAX/b (b==0 is safe: short-circuit).
 * Without __int128: full MULT_128_256 then require the high half to be zero.
 */
static inline int MULT_128_128(uint128_t a_128_bit, uint128_t b_128_bit, uint128_t* c_128_bit){
    int overflow_flag=0;

#ifdef DAP_GLOBAL_IS_INT128
    *c_128_bit= a_128_bit * b_128_bit;
    overflow_flag=(b_128_bit && a_128_bit>((uint128_t)-1)/b_128_bit);
#else
    int equal_flag=0;
    uint256_t full_product_256 = GET_256_FROM_64(0);
    MULT_128_256(a_128_bit,b_128_bit,&full_product_256);
    *c_128_bit=full_product_256.lo;
    equal_flag=EQUAL_128(full_product_256.hi,uint128_0);
    if (!equal_flag) {
        overflow_flag=1;
    }
#endif
    return overflow_flag;
}

/*
 * MULT_256_512 -- full unsigned 256x256 -> 512 bits.
 *
 * Split on 128-bit halves (Karatsubo-style layout):
 *   (a.hi*2^128 + a.lo) * (b.hi*2^128 + b.lo)
 * = a.lo*b.lo + (a.hi*b.lo + a.lo*b.hi)*2^128 + a.hi*b.hi*2^256.
 *
 * Four MULT_128_256 calls (ll, hh, hl, lh) are separate inline instances on purpose: at -O3 the
 * compiler can interleave independent multiply chains from different asm blocks, recovering
 * instruction-level parallelism. A single flat 64-bit limb loop serializes carry along each row
 * and is usually slower on out-of-order cores.
 *
 * hl+lh may sum to 257 bits (cc into the high half); packing into uint512_t must propagate cc and
 * the c1/c2 carries when adding ll.hi+cross.lo, hh.lo+cross.hi+c1, etc. Dropping those carries
 * corrupts the upper 256 bits of the product.
 */
static inline void MULT_256_512(uint256_t a_256_bit,uint256_t b_256_bit,uint512_t* c_512_bit) {
    uint256_t ll, hh, hl, lh;
    MULT_128_256(a_256_bit.lo, b_256_bit.lo, &ll);
    MULT_128_256(a_256_bit.hi, b_256_bit.hi, &hh);
    MULT_128_256(a_256_bit.hi, b_256_bit.lo, &hl);
    MULT_128_256(a_256_bit.lo, b_256_bit.hi, &lh);
    uint256_t cross;
    int cc = SUM_256_256(hl, lh, &cross);
#ifdef DAP_GLOBAL_IS_INT128
    c_512_bit->lo.lo = ll.lo;
    uint128_t s = ll.hi + cross.lo;
    int c1 = (s < ll.hi);
    c_512_bit->lo.hi = s;
    s = hh.lo + cross.hi;
    int c2 = (s < hh.lo);
    s += c1;
    c2 |= (s < (uint128_t)c1);
    c_512_bit->hi.lo = s;
    c_512_bit->hi.hi = hh.hi + c2 + cc;
#else
    c_512_bit->lo.lo = ll.lo;
    int c1 = SUM_128_128(ll.hi, cross.lo, &c_512_bit->lo.hi);
    uint128_t tmp;
    int c2 = SUM_128_128(hh.lo, cross.hi, &tmp);
    c2 |= SUM_128_128(tmp, GET_128_FROM_64(c1), &c_512_bit->hi.lo);
    SUM_128_128(hh.hi, GET_128_FROM_64(c2 + cc), &c_512_bit->hi.hi);
#endif
}

/*
 * MULT_256_256 -- low 256 bits of the product; overflow==1 if the high 256 bits of the full
 * 512-bit product are non-zero. Implemented only via a correct MULT_256_512; older code that
 * dropped cross-term carries could report overflow==0 when the product was actually wider than 256 bits.
 */
static inline int MULT_256_256(uint256_t a_256_bit,uint256_t b_256_bit,uint256_t* accum_256_bit){
    int overflow=0;
    int equal_flag=0;
    uint512_t full_product_512={.hi=uint256_0,.lo=uint256_0,};
    MULT_256_512(a_256_bit,b_256_bit,&full_product_512);
    *accum_256_bit=full_product_512.lo;
    equal_flag=EQUAL_256(full_product_512.hi,uint256_0);
    if (!equal_flag)
    {
        overflow=1;
    }
    return overflow;
}

/* ------------------------------------------------------------------------- */
/* Leading zeros / bit-scan (nlz, fls)                                        */
/* ------------------------------------------------------------------------- */

static inline int nlz64(uint64_t N)
{
#if defined(__GNUC__) || defined(__clang__)
    return N ? __builtin_clzll(N) : 64;
#else
    int c = 0;
    if (N == 0) return 64;
    while ((N & ((uint64_t)1 << 63)) == 0) { N <<= 1; c++; }
    return c;
#endif
}

static inline int nlz128(uint128_t N)
{
#ifdef DAP_GLOBAL_IS_INT128
    return ( (N >> 64) == 0) ? nlz64((uint64_t)N) + 64 : nlz64((uint64_t)(N >> 64));
#else
    return (N.hi == 0) ? nlz64(N.lo) + 64 : nlz64(N.hi);
#endif
}

static inline int nlz256(uint256_t N)
{
    return EQUAL_128(N.hi, uint128_0) ? nlz128(N.lo) + 128 : nlz128(N.hi);
}

static inline int fls256(uint256_t n) {
    if ( compare128(n.hi, uint128_0) != 0 ) {
        return 255 - nlz128(n.hi);
    }
    return 127 - nlz128(n.lo);
}

/* ------------------------------------------------------------------------- */
/* Multi-word division: limb types, Knuth Algorithm D (TAOCP vol.2 §4.3.1),   */
/* dap_divmod_* API                                                           */
/* ------------------------------------------------------------------------- */
/*
 * dap_math_kw_t is one "digit" for Knuth algorithm D: with __int128 it is uint64_t (256 bits = 4
 * words), else uint32_t (8 words). dap_math_kw2_t is a double digit for products and the trial
 * quotient inside each step.
 *
 * dap_u256_to_kw / dap_kw_to_u256 pack into little-endian limbs (w[0] is least significant).
 * Fast paths such as dap_divmod_u256_u64 for divisors that fit in 64 bits reduce to schoolbook
 * division one limb at a time (on x86-64 using a divq chain via dap_div_u128_u64).
 */

#ifdef DAP_GLOBAL_IS_INT128
typedef uint64_t dap_math_kw_t;
typedef uint128_t dap_math_kw2_t;
typedef int128_t dap_math_kw2s_t;
#define DAP_MATH_KW_BITS 64
#define DAP_MATH_KW_CLZ __builtin_clzll
#define DAP_MATH_KW_N256 4
#define DAP_MATH_KW_N512 8
#else
typedef uint32_t dap_math_kw_t;
typedef uint64_t dap_math_kw2_t;
typedef int64_t dap_math_kw2s_t;
#define DAP_MATH_KW_BITS 32
#define DAP_MATH_KW_CLZ __builtin_clz
#define DAP_MATH_KW_N256 8
#define DAP_MATH_KW_N512 16
#define DAP_MATH_KW_N128 4
#endif

static inline void dap_u256_to_kw(uint256_t a, dap_math_kw_t w[DAP_MATH_KW_N256])
{
#ifdef DAP_GLOBAL_IS_INT128
    w[0] = (uint64_t)a.lo;
    w[1] = (uint64_t)(a.lo >> 64);
    w[2] = (uint64_t)a.hi;
    w[3] = (uint64_t)(a.hi >> 64);
#else
    w[0] = (uint32_t)a.lo.lo;
    w[1] = (uint32_t)(a.lo.lo >> 32);
    w[2] = (uint32_t)a.lo.hi;
    w[3] = (uint32_t)(a.lo.hi >> 32);
    w[4] = (uint32_t)a.hi.lo;
    w[5] = (uint32_t)(a.hi.lo >> 32);
    w[6] = (uint32_t)a.hi.hi;
    w[7] = (uint32_t)(a.hi.hi >> 32);
#endif
}

static inline uint256_t dap_kw_to_u256(const dap_math_kw_t w[DAP_MATH_KW_N256])
{
    uint256_t r;
#ifdef DAP_GLOBAL_IS_INT128
    r.lo = (uint128_t)w[0] | ((uint128_t)w[1] << 64);
    r.hi = (uint128_t)w[2] | ((uint128_t)w[3] << 64);
#else
    r.lo.lo = (uint64_t)w[0] | ((uint64_t)w[1] << 32);
    r.lo.hi = (uint64_t)w[2] | ((uint64_t)w[3] << 32);
    r.hi.lo = (uint64_t)w[4] | ((uint64_t)w[5] << 32);
    r.hi.hi = (uint64_t)w[6] | ((uint64_t)w[7] << 32);
#endif
    return r;
}

static inline void dap_u512_to_kw(uint512_t a, dap_math_kw_t w[DAP_MATH_KW_N512])
{
    dap_u256_to_kw(a.lo, w);
    dap_u256_to_kw(a.hi, w + DAP_MATH_KW_N256);
}

static inline uint512_t dap_kw_to_u512(const dap_math_kw_t w[DAP_MATH_KW_N512])
{
    uint512_t r;
    r.lo = dap_kw_to_u256(w);
    r.hi = dap_kw_to_u256(w + DAP_MATH_KW_N256);
    return r;
}

/*
 * dap_div_u128_u64 -- divide 128-bit dividend (hi:lo) by 64-bit d; quotient returned, remainder in *rem.
 * Knuth precondition: high part of dividend < d, otherwise divq faults (#DE) or the trial quotient is wrong.
 * Used from dap_knuth_divmnu and dap_divmod_by_single_kw on 64-bit limbs.
 *
 * x86-64: single divq divides (rdx:rax) by the operand; we pass lo in rax, hi in rdx.
 * Fastest and simplest on this ISA.
 *
 * AArch64: no single-instruction 128/64 in user mode; plain (uint128_t)/(uint64_t) often lowers to a
 * costly libcall __udivti3. The Hacker's Delight §9-4 style path on 32-bit half-digits uses only
 * hardware 64-bit divides and avoids libgcc for this hotspot.
 *
 * Other targets with __int128: native widened division is portable; may become a runtime call but stays
 * correct without platform asm.
 */
#if defined(DAP_GLOBAL_IS_INT128) && defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
static inline uint64_t dap_div_u128_u64(uint64_t hi, uint64_t lo, uint64_t d, uint64_t *rem)
{
    uint64_t q;
    __asm__("divq %4" : "=a"(q), "=d"(*rem) : "a"(lo), "d"(hi), "rm"(d));
    return q;
}
#elif defined(DAP_GLOBAL_IS_INT128) && defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
static inline uint64_t dap_div_u128_u64(uint64_t u1, uint64_t u0, uint64_t v, uint64_t *rem)
{
    /* Hacker's Delight §9-4 style 128/64, 32-bit digits; avoids libgcc __udivti3 on AArch64. */
    const uint64_t B = (uint64_t)1 << 32;
    int s = __builtin_clzll(v);
    v <<= s;
    uint64_t un32 = s ? ((u1 << s) | (u0 >> (64 - s))) : u1;
    uint64_t un10 = u0 << s;
    uint64_t vn1 = v >> 32, vn0 = v & 0xFFFFFFFF;
    uint64_t un1 = un10 >> 32, un0 = un10 & 0xFFFFFFFF;
    uint64_t q1 = un32 / vn1, rhat = un32 % vn1;
    while (q1 >= B || q1 * vn0 > (rhat << 32 | un1)) {
        q1--;
        rhat += vn1;
        if (rhat >= B) break;
    }
    uint64_t un21 = (rhat << 32 | un1) - q1 * vn0;
    uint64_t q0 = un21 / vn1;
    rhat = un21 % vn1;
    while (q0 >= B || q0 * vn0 > (rhat << 32 | un0)) {
        q0--;
        rhat += vn1;
        if (rhat >= B) break;
    }
    *rem = ((rhat << 32 | un0) - q0 * vn0) >> s;
    return (q1 << 32) | q0;
}
#elif defined(DAP_GLOBAL_IS_INT128)
static inline uint64_t dap_div_u128_u64(uint64_t hi, uint64_t lo, uint64_t d, uint64_t *rem)
{
    uint128_t cur = ((uint128_t)hi << 64) | lo;
    *rem = (uint64_t)(cur % d);
    return (uint64_t)(cur / d);
}
#endif

/*
 * dap_knuth_divmnu -- Knuth's Algorithm D (TAOCP vol.2 §4.3.1): quotient q and remainder r for
 * unsigned u (m limbs) and v (n limbs), m >= n >= 2, v[n-1] != 0.
 *
 * Limbs are little-endian: u[0], v[0] are least significant. Left-shifting v by CLZ(v[n-1]) normalizes
 * so the top bit of v[n-1] is 1; then the trial quotient from two limbs of u and one of v stays
 * bounded, and Knuth's correction loops fix any overestimate. With 64-bit limbs the trial step uses
 * dap_div_u128_u64; with 32-bit limbs it uses native 64/32 (dividend in dap_math_kw2_t).
 *
 * The guard un[j+n] >= vn[n-1] avoids trial qhat == 2^KW_BITS-1 followed by multiply overflow in the
 * inner steps. The rhat < B check in the correction loop prevents shifting rhat past the digit width.
 */
static inline void dap_knuth_divmnu(dap_math_kw_t q[], dap_math_kw_t r[],
    const dap_math_kw_t u[], const dap_math_kw_t v[], int m, int n)
{
    const dap_math_kw2_t B = (dap_math_kw2_t)1 << DAP_MATH_KW_BITS;
    dap_math_kw_t vn[DAP_MATH_KW_N256], un[DAP_MATH_KW_N512 + 1];
    int s = DAP_MATH_KW_CLZ(v[n - 1]);
    if (s > 0) {
        for (int i = n - 1; i > 0; i--)
            vn[i] = (v[i] << s) | (v[i - 1] >> (DAP_MATH_KW_BITS - s));
        vn[0] = v[0] << s;
        un[m] = u[m - 1] >> (DAP_MATH_KW_BITS - s);
        for (int i = m - 1; i > 0; i--)
            un[i] = (u[i] << s) | (u[i - 1] >> (DAP_MATH_KW_BITS - s));
        un[0] = u[0] << s;
    } else {
        for (int i = 0; i < n; i++) vn[i] = v[i];
        for (int i = 0; i < m; i++) un[i] = u[i];
        un[m] = 0;
    }
    for (int j = m - n; j >= 0; j--) {
        dap_math_kw2_t qhat, rhat;
#ifdef DAP_GLOBAL_IS_INT128
        if (un[j + n] >= vn[n - 1]) {
            qhat = (dap_math_kw2_t)(dap_math_kw_t)(-1);
            rhat = (dap_math_kw2_t)un[j + n - 1] + vn[n - 1];
        } else {
            uint64_t rhat64;
            qhat = dap_div_u128_u64(un[j + n], un[j + n - 1], vn[n - 1], &rhat64);
            rhat = rhat64;
        }
#else
        dap_math_kw2_t dividend = (dap_math_kw2_t)un[j + n] << DAP_MATH_KW_BITS | un[j + n - 1];
        qhat = dividend / vn[n - 1];
        rhat = dividend % vn[n - 1];
#endif
        while (qhat >= B || (rhat < B && qhat * vn[n - 2] > ((rhat << DAP_MATH_KW_BITS) | un[j + n - 2]))) {
            qhat--;
            rhat += vn[n - 1];
            if (rhat >= B) break;
        }
        dap_math_kw2s_t borrow = 0;
        for (int i = 0; i < n; i++) {
            dap_math_kw2_t prod = (dap_math_kw2_t)(dap_math_kw_t)qhat * vn[i];
            dap_math_kw2s_t diff = (dap_math_kw2s_t)un[i + j] - borrow - (dap_math_kw2s_t)(dap_math_kw_t)prod;
            un[i + j] = (dap_math_kw_t)diff;
            borrow = (dap_math_kw2s_t)(prod >> DAP_MATH_KW_BITS) - (diff >> DAP_MATH_KW_BITS);
        }
        dap_math_kw2s_t diff = (dap_math_kw2s_t)un[j + n] - borrow;
        un[j + n] = (dap_math_kw_t)diff;
        q[j] = (dap_math_kw_t)qhat;
        if (diff < 0) {
            q[j]--;
            dap_math_kw_t carry = 0;
            for (int i = 0; i < n; i++) {
                dap_math_kw2_t sum = (dap_math_kw2_t)un[i + j] + vn[i] + carry;
                un[i + j] = (dap_math_kw_t)sum;
                carry = (dap_math_kw_t)(sum >> DAP_MATH_KW_BITS);
            }
            un[j + n] += carry;
        }
    }
    if (r) {
        if (s > 0) {
            for (int i = 0; i < n - 1; i++)
                r[i] = (un[i] >> s) | (un[i + 1] << (DAP_MATH_KW_BITS - s));
            r[n - 1] = un[n - 1] >> s;
        } else {
            for (int i = 0; i < n; i++) r[i] = un[i];
        }
    }
}

static inline void dap_divmod_by_single_kw(const dap_math_kw_t u[], int m, dap_math_kw_t d,
    dap_math_kw_t q[], dap_math_kw_t *rem)
{
#ifdef DAP_GLOBAL_IS_INT128
    uint64_t rr = 0;
    for (int i = m - 1; i >= 0; i--)
        q[i] = dap_div_u128_u64(rr, u[i], d, &rr);
    *rem = (dap_math_kw_t)rr;
#else
    dap_math_kw2_t rr = 0;
    for (int i = m - 1; i >= 0; i--) {
        dap_math_kw2_t cur = (rr << DAP_MATH_KW_BITS) | u[i];
        q[i] = (dap_math_kw_t)(cur / d);
        rr = cur % d;
    }
    *rem = (dap_math_kw_t)rr;
#endif
}

#ifndef DAP_GLOBAL_IS_INT128
static inline void dap_u128_to_kw(uint128_t a, dap_math_kw_t w[DAP_MATH_KW_N128])
{
    w[0] = (dap_math_kw_t)(uint32_t)a.lo;
    w[1] = (dap_math_kw_t)(uint32_t)(a.lo >> 32);
    w[2] = (dap_math_kw_t)(uint32_t)a.hi;
    w[3] = (dap_math_kw_t)(uint32_t)(a.hi >> 32);
}

static inline uint128_t dap_kw_to_u128(const dap_math_kw_t w[DAP_MATH_KW_N128])
{
    uint128_t r;
    r.lo = (uint64_t)w[0] | ((uint64_t)w[1] << 32);
    r.hi = (uint64_t)w[2] | ((uint64_t)w[3] << 32);
    return r;
}

/* true = division by zero */
static inline bool dap_divmod_u128(uint128_t a_num, uint128_t a_den, uint128_t *a_q, uint128_t *a_r)
{
    if (IS_ZERO_128(a_den)) {
        *a_q = uint128_0;
        *a_r = uint128_0;
        return true;
    }
    int cmp = compare128(a_den, a_num);
    if (cmp == 1) {
        *a_q = uint128_0;
        *a_r = a_num;
        return false;
    }
    if (cmp == 0) {
        *a_q = uint128_1;
        *a_r = uint128_0;
        return false;
    }
    dap_math_kw_t v[DAP_MATH_KW_N128], u[DAP_MATH_KW_N128];
    dap_u128_to_kw(a_den, v);
    int n = DAP_MATH_KW_N128;
    while (n > 1 && v[n - 1] == 0) n--;
    dap_u128_to_kw(a_num, u);
    int m = DAP_MATH_KW_N128;
    while (m > 1 && u[m - 1] == 0) m--;
    if (m < n) {
        *a_q = uint128_0;
        *a_r = a_num;
        return false;
    }
    dap_math_kw_t qw[DAP_MATH_KW_N128] = { 0 }, rw[DAP_MATH_KW_N128] = { 0 };
    if (n == 1) {
        dap_math_kw_t rm;
        dap_divmod_by_single_kw(u, m, v[0], qw, &rm);
        *a_q = dap_kw_to_u128(qw);
        *a_r = GET_128_FROM_64((uint64_t)rm);
        return false;
    }
    dap_knuth_divmnu(qw, rw, u, v, m, n);
    *a_q = dap_kw_to_u128(qw);
    *a_r = dap_kw_to_u128(rw);
    return false;
}
#endif

static inline void dap_divmod_u256_u64(uint256_t a, uint64_t d, uint256_t *q, uint64_t *rem)
{
    dap_math_kw_t u[DAP_MATH_KW_N256], qw[DAP_MATH_KW_N256];
    for (int i = 0; i < DAP_MATH_KW_N256; i++) qw[i] = 0;
    dap_u256_to_kw(a, u);
#ifdef DAP_GLOBAL_IS_INT128
    dap_math_kw_t rm;
    dap_divmod_by_single_kw(u, DAP_MATH_KW_N256, (dap_math_kw_t)d, qw, &rm);
    *q = dap_kw_to_u256(qw);
    *rem = (uint64_t)rm;
#else
    dap_math_kw_t v[2] = { (uint32_t)d, (uint32_t)(d >> 32) };
    int n = v[1] ? 2 : 1, m = DAP_MATH_KW_N256;
    while (m > n && u[m - 1] == 0) m--;
    if (m < n) {
        *q = uint256_0;
        *rem = 0;
        return;
    }
    if (n == 1) {
        dap_math_kw_t rm;
        dap_divmod_by_single_kw(u, m, v[0], qw, &rm);
        *q = dap_kw_to_u256(qw);
        *rem = (uint64_t)rm;
    } else {
        dap_math_kw_t rw[2] = { 0 };
        dap_knuth_divmnu(qw, rw, u, v, m, n);
        *q = dap_kw_to_u256(qw);
        *rem = (uint64_t)rw[0] | ((uint64_t)rw[1] << 32);
    }
#endif
}

static inline void dap_divmod_u512_u64(uint512_t a, uint64_t d, uint512_t *q, uint64_t *rem)
{
    dap_math_kw_t u[DAP_MATH_KW_N512], qw[DAP_MATH_KW_N512];
    for (int i = 0; i < DAP_MATH_KW_N512; i++) qw[i] = 0;
    dap_u512_to_kw(a, u);
#ifdef DAP_GLOBAL_IS_INT128
    dap_math_kw_t rm;
    dap_divmod_by_single_kw(u, DAP_MATH_KW_N512, (dap_math_kw_t)d, qw, &rm);
    *q = dap_kw_to_u512(qw);
    *rem = (uint64_t)rm;
#else
    dap_math_kw_t v[2] = { (uint32_t)d, (uint32_t)(d >> 32) };
    int n = v[1] ? 2 : 1, m = DAP_MATH_KW_N512;
    while (m > n && u[m - 1] == 0) m--;
    if (m < n) {
        *q = uint512_0;
        *rem = 0;
        return;
    }
    if (n == 1) {
        dap_math_kw_t rm;
        dap_divmod_by_single_kw(u, m, v[0], qw, &rm);
        *q = dap_kw_to_u512(qw);
        *rem = (uint64_t)rm;
    } else {
        dap_math_kw_t rw[2] = { 0 };
        dap_knuth_divmnu(qw, rw, u, v, m, n);
        *q = dap_kw_to_u512(qw);
        *rem = (uint64_t)rw[0] | ((uint64_t)rw[1] << 32);
    }
#endif
}

/* true = division by zero */
static inline bool dap_divmod_u256(uint256_t a_num, uint256_t a_den, uint256_t *a_q, uint256_t *a_r)
{
    if (IS_ZERO_256(a_den)) {
        *a_q = uint256_0;
        *a_r = uint256_0;
        return true;
    }
    int cmp = compare256(a_den, a_num);
    if (cmp == 1) {
        *a_q = uint256_0;
        *a_r = a_num;
        return false;
    }
    if (cmp == 0) {
        *a_q = uint256_1;
        *a_r = uint256_0;
        return false;
    }
    dap_math_kw_t v[DAP_MATH_KW_N256];
    dap_u256_to_kw(a_den, v);
    int n = DAP_MATH_KW_N256;
    while (n > 1 && v[n - 1] == 0) n--;
#ifdef DAP_GLOBAL_IS_INT128
    if (n == 1) {
        uint64_t rem;
        dap_divmod_u256_u64(a_num, v[0], a_q, &rem);
        *a_r = GET_256_FROM_64(rem);
        return false;
    }
#else
    if (n <= 2) {
        uint64_t d64 = (n == 1) ? v[0] : ((uint64_t)v[0] | ((uint64_t)v[1] << 32));
        uint64_t rem;
        dap_divmod_u256_u64(a_num, d64, a_q, &rem);
        *a_r = GET_256_FROM_64(rem);
        return false;
    }
#endif
    dap_math_kw_t u[DAP_MATH_KW_N256];
    dap_u256_to_kw(a_num, u);
    dap_math_kw_t qq[DAP_MATH_KW_N256] = { 0 }, rr[DAP_MATH_KW_N256] = { 0 };
    dap_knuth_divmnu(qq, rr, u, v, DAP_MATH_KW_N256, n);
    *a_q = dap_kw_to_u256(qq);
    *a_r = dap_kw_to_u256(rr);
    return false;
}

/* true = division by zero. Remainder fits in low 256 bits of dividend when quotient is 0 (m < n). */
static inline bool dap_divmod_u512_u256(uint512_t a_num, uint256_t a_den, uint256_t *a_q, uint256_t *a_r)
{
    if (IS_ZERO_256(a_den)) {
        *a_q = uint256_0;
        *a_r = uint256_0;
        return true;
    }
    dap_math_kw_t v[DAP_MATH_KW_N256];
    dap_u256_to_kw(a_den, v);
    int n = DAP_MATH_KW_N256;
    while (n > 1 && v[n - 1] == 0) n--;
#ifdef DAP_GLOBAL_IS_INT128
    if (n == 1) {
#else
    if (n <= 2) {
        uint64_t d64 = (n == 1) ? v[0] : ((uint64_t)v[0] | ((uint64_t)v[1] << 32));
#endif
        uint512_t q512;
        uint64_t rem;
#ifdef DAP_GLOBAL_IS_INT128
        dap_divmod_u512_u64(a_num, v[0], &q512, &rem);
#else
        dap_divmod_u512_u64(a_num, d64, &q512, &rem);
#endif
        *a_q = q512.lo;
        *a_r = GET_256_FROM_64(rem);
        return false;
    }
    dap_math_kw_t u[DAP_MATH_KW_N512];
    dap_u512_to_kw(a_num, u);
    int m = DAP_MATH_KW_N512;
    while (m > n && u[m - 1] == 0) m--;
    if (m < n) {
        *a_q = uint256_0;
        *a_r = a_num.lo;
        return false;
    }
    dap_math_kw_t qq[DAP_MATH_KW_N512] = { 0 }, rr[DAP_MATH_KW_N256] = { 0 };
    dap_knuth_divmnu(qq, rr, u, v, m, n);
    *a_q = dap_kw_to_u256(qq);
    *a_r = dap_kw_to_u256(rr);
    return false;
}

/* ------------------------------------------------------------------------- */
/* Widen 256×64 -> 512 (used by DIV_256_COIN)                                 */
/* ------------------------------------------------------------------------- */

/* 256-bit value times 64-bit -> full 512-bit product (for fixed-point COIN scaling). */
static inline void dap_mult256_u64_to_512(uint256_t a, uint64_t d, uint512_t *c)
{
#ifdef DAP_GLOBAL_IS_INT128
    uint64_t aw[4] = { (uint64_t)a.lo, (uint64_t)(a.lo >> 64), (uint64_t)a.hi, (uint64_t)(a.hi >> 64) };
    uint128_t carry = 0;
    uint64_t r[5];
    for (int i = 0; i < 4; i++) {
        uint128_t prod = (uint128_t)aw[i] * d + carry;
        r[i] = (uint64_t)prod;
        carry = prod >> 64;
    }
    r[4] = (uint64_t)carry;
    *c = uint512_0;
    c->lo.lo = (uint128_t)r[0] | ((uint128_t)r[1] << 64);
    c->lo.hi = (uint128_t)r[2] | ((uint128_t)r[3] << 64);
    c->hi.lo = (uint128_t)r[4];
#else
    uint64_t aw[4] = { a.lo.lo, a.lo.hi, a.hi.lo, a.hi.hi };
    uint64_t carry = 0, rw[5];
    for (int i = 0; i < 4; i++) {
        uint128_t prod;
        MULT_64_128(aw[i], d, &prod);
        uint64_t s = prod.lo + carry;
        rw[i] = s;
        carry = prod.hi + (s < prod.lo);
    }
    rw[4] = carry;
    *c = uint512_0;
    c->lo.lo = GET_128_FROM_64_64(rw[1], rw[0]);
    c->lo.hi = GET_128_FROM_64_64(rw[3], rw[2]);
    c->hi.lo = GET_128_FROM_64(rw[4]);
#endif
}

/* ------------------------------------------------------------------------- */
/* Legacy divmod / DIV (thin wrappers over dap_divmod_*)                      */
/* ------------------------------------------------------------------------- */

#ifndef DAP_GLOBAL_IS_INT128
static inline void divmod_impl_128(uint128_t a_dividend, uint128_t a_divisor, uint128_t *a_quotient, uint128_t *a_remainder)
{
    (void)dap_divmod_u128(a_dividend, a_divisor, a_quotient, a_remainder);
}
#endif

static inline void divmod_impl_256(uint256_t a_dividend, uint256_t a_divisor, uint256_t *a_quotient, uint256_t *a_remainder)
{
    (void)dap_divmod_u256(a_dividend, a_divisor, a_quotient, a_remainder);
}


static inline void DIV_128(uint128_t a_128_bit, uint128_t b_128_bit, uint128_t* c_128_bit){
    uint128_t l_ret = uint128_0;
#ifdef DAP_GLOBAL_IS_INT128
    if (!b_128_bit) {
        *c_128_bit = uint128_0;
        return;
    }
    l_ret = a_128_bit / b_128_bit;
#else
    uint128_t l_remainder = uint128_0;
    divmod_impl_128(a_128_bit, b_128_bit, &l_ret, &l_remainder);
#endif
    *c_128_bit = l_ret;
}

static inline void DIV_256(uint256_t a_256_bit, uint256_t b_256_bit, uint256_t* c_256_bit){
    uint256_t l_ret = uint256_0;
    uint256_t l_remainder = uint256_0;
    divmod_impl_256(a_256_bit, b_256_bit, &l_ret, &l_remainder);
    *c_256_bit = l_ret;
}

/* ------------------------------------------------------------------------- */
/* Fixed-point COIN (10^18)                                                   */
/* ------------------------------------------------------------------------- */

/**
 * Multiplicates to fixed-point values, represented as 256-bit values
 * @param a_val
 * @param b_val
 * @param result is a fixed-point value, represented as 256-bit value
 * @return
 */
static inline int _MULT_256_COIN(uint256_t a_val, uint256_t b_val, uint256_t* result, bool round_result) {
    uint512_t wide;
    MULT_256_512(a_val, b_val, &wide);
    uint512_t q;
    uint64_t rem64;
    dap_divmod_u512_u64(wide, 1000000000000000000ULL, &q, &rem64);
    *result = q.lo;
    int overflow = !IS_ZERO_256(q.hi);
    if (round_result) {
        uint256_t rem = GET_256_FROM_64(rem64),
                  five = GET_256_FROM_64(500000000000000000);
        if (compare256(rem, five) >= 0)
            SUM_256_256(*result, uint256_1, result);
    }
    return overflow;
}

#define MULT_256_COIN(a_val,b_val,res) _MULT_256_COIN(a_val,b_val,res,false)
/**
 * Divides two fixed-point values, represented as 256-bit values
 * @param a_val
 * @param b_val
 * @param result is a fixed-point value, represented as 256-bit value
 * @return
 */
static inline void DIV_256_COIN(uint256_t a, uint256_t b, uint256_t *res)
{
    if (IS_ZERO_256(b)) {
        *res = uint256_0;
        return;
    }
    if (compare256(a, uint256_0) == 0) {
        *res = uint256_0;
        return;
    }
    uint512_t wide;
    dap_mult256_u64_to_512(a, 1000000000000000000ULL, &wide);
    uint256_t rem;
    (void)dap_divmod_u512_u256(wide, b, res, &rem);
}

#ifdef __cplusplus
}
#endif
