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
    return EQUAL_256(a_256_bit, uint256_0);//a_256_bit.lo == (uint128_t)0;
}

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

static inline void LEFT_SHIFT_128(uint128_t a_128_bit,uint128_t* b_128_bit,int n){
    assert (n <= 128);

#ifdef DAP_GLOBAL_IS_INT128
    *b_128_bit= a_128_bit << n;

#else
    if (n >= 64) { // shifting 64-bit integer by more than 63 bits is not defined
        a_128_bit.hi=a_128_bit.lo;
        a_128_bit.lo=0;
        LEFT_SHIFT_128(a_128_bit,b_128_bit,n-64);
    }
    else if (n == 0) {
       b_128_bit->hi=a_128_bit.hi;
       b_128_bit->lo=a_128_bit.lo;
    }
    else {
        uint64_t shift_temp;
        shift_temp=a_128_bit.lo<<n;
        b_128_bit->lo=shift_temp;
        b_128_bit->hi=(a_128_bit.hi<<n)|(a_128_bit.lo>>(64-n));
    }

#endif
}

static inline void RIGHT_SHIFT_128(uint128_t a_128_bit,uint128_t* b_128_bit,int n){
    assert (n <= 128);

#ifdef DAP_GLOBAL_IS_INT128
    (*b_128_bit) = a_128_bit >> n;
#else
    if (n >= 64) // shifting 64-bit integer by more than 63 bits is not defined
    {
        a_128_bit.lo=a_128_bit.hi;
        a_128_bit.hi=0;
        RIGHT_SHIFT_128(a_128_bit,b_128_bit,n-64);
    }
    else if (n == 0)
    {
       b_128_bit->hi=a_128_bit.hi;
       b_128_bit->lo=a_128_bit.lo;
    }
    else
    {   uint64_t shift_temp;
        shift_temp=a_128_bit.hi>>n;
        b_128_bit->hi=shift_temp;
        b_128_bit->lo=(a_128_bit.lo>>n)|(a_128_bit.hi<<(64-n));
    }
#endif
}

static inline void LEFT_SHIFT_256(uint256_t a_256_bit,uint256_t* b_256_bit,int n){

    assert (n <= 256);

    if (n >= 128) { // shifting 64-bit integer by more than 63 bits is not defined
        a_256_bit.hi=a_256_bit.lo;
        a_256_bit.lo=uint128_0;
        LEFT_SHIFT_256(a_256_bit,b_256_bit,n-128);
    }
    if (n == 0) {
        b_256_bit->hi=a_256_bit.hi;
        b_256_bit->lo=a_256_bit.lo;
    }
    else if (n<128) {
        uint128_t shift_temp=uint128_0;
        LEFT_SHIFT_128(a_256_bit.lo,&shift_temp,n);
        b_256_bit->lo=shift_temp;
        uint128_t shift_temp_or_left=uint128_0;
        uint128_t shift_temp_or_right=uint128_0;
        LEFT_SHIFT_128(a_256_bit.hi,&shift_temp_or_left,n);
        RIGHT_SHIFT_128(a_256_bit.lo,&shift_temp_or_right,128-n);
        b_256_bit->hi=OR_128(shift_temp_or_left,shift_temp_or_right);
    }
}

static inline void RIGHT_SHIFT_256(uint256_t a_256_bit,uint256_t* b_256_bit,int n){
    assert (n <= 256);
    if (n >= 128) { // shifting 64-bit integer by more than 63 bits is not defined
        a_256_bit.lo=a_256_bit.hi;
        a_256_bit.hi=uint128_0;
        RIGHT_SHIFT_256(a_256_bit,b_256_bit,n-128);
    }
    if (n == 0) {
        b_256_bit->hi=a_256_bit.hi;
        b_256_bit->lo=a_256_bit.lo;
    }
    else if (n<128) {
        uint128_t shift_temp=uint128_0;
        RIGHT_SHIFT_128(a_256_bit.hi,&shift_temp,n);
        b_256_bit->hi=shift_temp;
        uint128_t shift_temp_or_left=uint128_0;
        uint128_t shift_temp_or_right=uint128_0;
        RIGHT_SHIFT_128(a_256_bit.lo,&shift_temp_or_left,n);
        LEFT_SHIFT_128(a_256_bit.hi,&shift_temp_or_right,128-n);
        b_256_bit->lo=OR_128(shift_temp_or_left,shift_temp_or_right);
    }
}

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
    return (int)(a_64_bit > ((uint64_t)-1) / b_64_bit);
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
    uint64_t carry = (((c_128_bit->lo & b_128_bit.lo) & 1) + (b_128_bit.lo >> 1) + (c_128_bit->lo >> 1)) >> 63;
    c_128_bit->hi = a_128_bit.hi - (b_128_bit.hi + carry);
    underflow_flag=carry;
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

static inline int SUM_256_256(uint256_t a_256_bit,uint256_t b_256_bit,uint256_t* c_256_bit)
{
    int overflow_flag=0;
    uint128_t intermediate_value = uint128_0;
#ifdef DAP_GLOBAL_IS_INT128
    int overflow_flag_intermediate;
    overflow_flag=SUM_128_128(a_256_bit.lo,b_256_bit.lo,&intermediate_value);
    c_256_bit->lo = intermediate_value;
    uint128_t carry_in_128=overflow_flag;
    overflow_flag=0;
    overflow_flag=SUM_128_128(carry_in_128,a_256_bit.hi,&intermediate_value);
    overflow_flag_intermediate=SUM_128_128(intermediate_value,b_256_bit.hi,&intermediate_value);
    c_256_bit->hi = intermediate_value;
    overflow_flag |= overflow_flag_intermediate;
#else
    uint256_t tmp = uint256_0;
    overflow_flag=SUM_128_128(a_256_bit.lo,b_256_bit.lo,&tmp.lo);
    uint128_t carry_in_128;
    carry_in_128.hi=0;
    carry_in_128.lo=overflow_flag;
    overflow_flag=0;
    overflow_flag=SUM_128_128(carry_in_128,a_256_bit.hi,&intermediate_value);
    //we store overflow_flag in case there is already overflow
    int overflow_flag_bis=0;
    overflow_flag_bis=SUM_128_128(intermediate_value,b_256_bit.hi,&tmp.hi);
    c_256_bit->hi = tmp.hi;
    c_256_bit->lo = tmp.lo;
    overflow_flag |= overflow_flag_bis;
#endif
    return overflow_flag;
}

static inline int SUBTRACT_256_256(uint256_t a_256_bit,uint256_t b_256_bit,uint256_t* c_256_bit)
{
#ifdef DAP_GLOBAL_IS_INT128
    int underflow_flag=0;
    c_256_bit->lo = a_256_bit.lo - b_256_bit.lo;
    uint64_t carry = (((c_256_bit->lo & b_256_bit.lo) & 1) + (b_256_bit.lo >> 1) + (c_256_bit->lo >> 1)) >> 127;
    c_256_bit->hi = a_256_bit.hi - (b_256_bit.hi + carry);
    underflow_flag=carry;
    return underflow_flag;

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

static inline void MULT_128_256(uint128_t a_128_bit,uint128_t b_128_bit,uint256_t* c_256_bit ) {
#ifdef DAP_GLOBAL_IS_INT128
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

static inline int MULT_128_128(uint128_t a_128_bit, uint128_t b_128_bit, uint128_t* c_128_bit){
    int overflow_flag=0;

#ifdef DAP_GLOBAL_IS_INT128
    *c_128_bit= a_128_bit * b_128_bit;
    overflow_flag=(a_128_bit>((uint128_t)-1)/b_128_bit);
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

// we have test fails for this function with 512 bit, need to check it out if using it with 512 bit space
// But with 256 bit sapce it works correct
static inline void MULT_256_512(uint256_t a_256_bit,uint256_t b_256_bit,uint512_t* c_512_bit) {
    int dummy_overflow;
    //product of .hi terms - stored in .hi field of c_512_bit
    MULT_128_256(a_256_bit.hi,b_256_bit.hi, &c_512_bit->hi);

    //product of .lo terms - stored in .lo field of c_512_bit
    MULT_128_256(a_256_bit.lo,b_256_bit.lo, &c_512_bit->lo);

    //cross product of .hi and .lo terms
    uint256_t cross_product_first_term=uint256_0;
    uint256_t cross_product_second_term=uint256_0;
    uint256_t cross_product=uint256_0;
    uint256_t cross_product_shift_128=uint256_0;
    uint256_t c_512_bit_lo_copy=uint256_0;
    uint256_t c_512_bit_hi_copy=uint256_0;
    int overflow=0;

    MULT_128_256(a_256_bit.hi,b_256_bit.lo,&cross_product_first_term);
    MULT_128_256(a_256_bit.lo,b_256_bit.hi,&cross_product_second_term);
    overflow=SUM_256_256(cross_product_first_term,cross_product_second_term,&cross_product);

    LEFT_SHIFT_256(cross_product,&cross_product_shift_128,128); //the factor in front of cross product is 2**128
    c_512_bit_lo_copy=c_512_bit->lo;
    dummy_overflow=SUM_256_256(c_512_bit_lo_copy,cross_product_shift_128,&c_512_bit->lo);

    cross_product_shift_128.hi = uint128_0;
    cross_product_shift_128.lo = uint128_0;
    RIGHT_SHIFT_256(cross_product,&cross_product_shift_128,128);
    c_512_bit_hi_copy=c_512_bit->hi;
    dummy_overflow=SUM_256_256(c_512_bit_hi_copy,cross_product_shift_128,&c_512_bit->hi);
    // TODO
    UNUSED(overflow);
    UNUSED(dummy_overflow);
}

/* Multiplicates 256-bit value to fixed-point value, represented as 256-bit value
 * @param a_val
 * @param a_mult
 * @param result is a fixed-point value, represented as 256-bit value
 * @return
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

// > ret 1
// == ret 0
// < ret -1
static inline int compare128(uint128_t a, uint128_t b)
{
#ifdef DAP_GLOBAL_IS_INT128
    return ( a > b ? 1 : 0 ) - ( a < b ? 1 : 0 );
#else
    return    (((a.hi > b.hi) || ((a.hi == b.hi) && (a.lo > b.lo))) ? 1 : 0)
         -    (((a.hi < b.hi) || ((a.hi == b.hi) && (a.lo < b.lo))) ? 1 : 0);
#endif
}

static inline int compare256(uint256_t a, uint256_t b)
{
    return    (( compare128(a.hi, b.hi) == 1 || (compare128(a.hi, b.hi) == 0 && compare128(a.lo, b.lo) == 1)) ? 1 : 0)
              -    (( compare128(a.hi, b.hi) == -1 || (compare128(a.hi, b.hi) == 0 && compare128(a.lo, b.lo) == -1)) ? 1 : 0);
}

static inline int nlz64(uint64_t N)
{
    uint64_t I;
    size_t C;

    I = ~N;
    C = ((I ^ (I + 1)) & I) >> 63;

    I = (N >> 32) + 0xffffffff;
    I = ((I & 0x100000000) ^ 0x100000000) >> 27;
    C += I;  N <<= I;

    I = (N >> 48) + 0xffff;
    I = ((I & 0x10000) ^ 0x10000) >> 12;
    C += I;  N <<= I;

    I = (N >> 56) + 0xff;
    I = ((I & 0x100) ^ 0x100) >> 5;
    C += I;  N <<= I;

    I = (N >> 60) + 0xf;
    I = ((I & 0x10) ^ 0x10) >> 2;
    C += I;  N <<= I;

    I = (N >> 62) + 3;
    I = ((I & 4) ^ 4) >> 1;
    C += I;  N <<= I;

    C += (N >> 63) ^ 1;

    return C;
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


#ifndef DAP_GLOBAL_IS_INT128
static inline int fls128(uint128_t n) {
    if ( n.hi != 0 ) {
        return 127 - nlz64(n.hi);
    }
    return 63 - nlz64(n.lo);
}
//todo: this should not be ander ifndef
static inline void divmod_impl_128(uint128_t a_dividend, uint128_t a_divisor, uint128_t *a_quotient, uint128_t *a_remainder)
{
    assert( compare128(a_divisor, uint128_0) ); // a_divisor != 0
    if ( compare128(a_divisor, a_dividend) == 1 ) { // a_divisor > a_dividend
        *a_quotient = uint128_0;
        *a_remainder = a_dividend;
        return;
    }
    if ( compare128(a_divisor, a_dividend) == 0 ) { // a_divisor == a_dividend
        *a_quotient = uint128_1;
        *a_remainder = uint128_0;
        return;
    }

    uint128_t l_denominator = a_divisor;
    uint128_t l_quotient = uint128_0;
    int l_shift = fls128(a_dividend) - fls128(l_denominator);

    LEFT_SHIFT_128(l_denominator, &l_denominator, l_shift);

    for (int i = 0; i <= l_shift; ++i) {
        LEFT_SHIFT_128(l_quotient, &l_quotient, 1);

        if( compare128(a_dividend, l_denominator) >= 0 ) {
            SUBTRACT_128_128(a_dividend, l_denominator, &a_dividend);
            l_quotient = OR_128(l_quotient, uint128_1);  //l_quotient.lo |= 1;
        }
        RIGHT_SHIFT_128(l_denominator, &l_denominator, 1);
    }
    *a_quotient = l_quotient;
    *a_remainder = a_dividend;
}
#endif


static inline int fls256(uint256_t n) {
    if ( compare128(n.hi, uint128_0) != 0 ) {
        return 255 - nlz128(n.hi);
    }
    return 127 - nlz128(n.lo);
}

static inline void divmod_impl_256(uint256_t a_dividend, uint256_t a_divisor, uint256_t *a_quotient, uint256_t *a_remainder)
{
    assert( compare256(a_divisor, uint256_0) ); // a_divisor != 0
    if (!compare256(a_divisor, uint256_0)) {
        _log_it(NULL, 0, "dap_math_ops", L_ERROR, "An error occurred when trying to divide by 0.");
        raise(SIGFPE);
    }
    if ( compare256(a_divisor, a_dividend) == 1 ) { // a_divisor > a_dividend
        *a_quotient = uint256_0;
        *a_remainder = a_dividend;
        return;
    }
    if ( compare256(a_divisor, a_dividend) == 0 ) { // a_divisor == a_dividend
        *a_quotient = uint256_1;
        *a_remainder = uint256_0;
        return;
    }

    uint256_t l_denominator = a_divisor;
    uint256_t l_quotient = uint256_0;
    // int l_shift = nlz256(a_dividend) - nlz256(l_denominator);
    int l_shift = fls256(a_dividend) - fls256(l_denominator);
    LEFT_SHIFT_256(l_denominator, &l_denominator, l_shift);

    for (int i = 0; i <= l_shift; ++i) {
        LEFT_SHIFT_256(l_quotient, &l_quotient, 1);

        if( compare256(a_dividend, l_denominator) >= 0 ) {
            SUBTRACT_256_256(a_dividend, l_denominator, &a_dividend);
            l_quotient = OR_256(l_quotient, uint256_1);
        }
        RIGHT_SHIFT_256(l_denominator, &l_denominator, 1);
    }
    *a_quotient = l_quotient;
    *a_remainder = a_dividend;
}


static inline void DIV_128(uint128_t a_128_bit, uint128_t b_128_bit, uint128_t* c_128_bit){
    uint128_t l_ret = uint128_0;
#ifdef DAP_GLOBAL_IS_INT128
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

/**
 * Multiplicates to fixed-point values, represented as 256-bit values
 * @param a_val
 * @param b_val
 * @param result is a fixed-point value, represented as 256-bit value
 * @return
 */
static inline int _MULT_256_COIN(uint256_t a_val, uint256_t b_val, uint256_t* result, bool round_result) {
    uint256_t tmp, rem,
            five = GET_256_FROM_64(500000000000000000),
            ten18 = GET_256_FROM_64(1000000000000000000ULL);
    int overflow = MULT_256_256(a_val, b_val, &tmp);
    divmod_impl_256(tmp, ten18, &tmp, &rem);
    if (round_result && (compare256(rem, five) >= 0)) {
        SUM_256_256(tmp, uint256_1, &tmp);
    }
    *result = tmp;
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
    if (compare256(a, uint256_0) == 0) {
        *res = uint256_0;
        return;
    }
    int counter_integer = 0;
    int counter_fraction = 0;
    uint256_t a_copy = a;
    uint256_t ten18 = GET_256_FROM_64(1000000000000000000ULL);
    uint256_t ten = GET_256_FROM_64(10L);
    uint256_t rem = uint256_0;
    uint256_t loan = GET_256_FROM_64(1L);
    while (compare256(a_copy, b) < 0) {
        counter_integer++;
        MULT_256_256(a_copy, ten, &a_copy);
    }
//    DIV_256(a_copy, b, &a_copy);

    divmod_impl_256(a_copy, b, &a_copy, &rem);
    uint256_t fraction = uint256_0;
    uint256_t tmp = uint256_0;
    while (compare256(rem, uint256_0) > 0 && counter_fraction < 18) {
        counter_fraction++;
        MULT_256_256(fraction, ten, &fraction);
        MULT_256_256(rem, ten, &rem);
        divmod_impl_256(rem, b, &tmp, &rem);
        SUM_256_256(fraction, tmp, &fraction);
    }
    MULT_256_256(a_copy, ten18, &a_copy);
    while (counter_fraction < 18) {
        counter_fraction++;
        MULT_256_256(fraction, ten, &fraction);
    }
    SUM_256_256(a_copy, fraction, &a_copy);

    while(counter_integer--) {
        MULT_256_256(loan, ten, &loan); //maybe we should use same table as in dap_chain_common.c instead of cycle ?
    }
    DIV_256(a_copy, loan, &a_copy);
    *res = a_copy;
}

#ifdef __cplusplus
}
#endif
