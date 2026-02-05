/*
 * Generic (Portable C) secp256k1 Scalar Multiplication
 * Uses __uint128_t for 64x64->128 bit multiplication
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ecdsa_scalar.h"
#include "ecdsa_scalar_mul_arch.h"

// ============================================================================
// secp256k1 curve order n (for reduction)
// ============================================================================

static const uint64_t SCALAR_N[4] = {
    0xBFD25E8CD0364141ULL,
    0xBAAEDCE6AF48A03BULL,
    0xFFFFFFFFFFFFFFFEULL,
    0xFFFFFFFFFFFFFFFFULL
};

static const uint64_t SCALAR_2P256_MOD_N[4] = {
    0x402DA1732FC9BEBFULL,
    0x4551231950B75FC4ULL,
    0x0000000000000001ULL,
    0x0000000000000000ULL
};

// ============================================================================
// 256x256 -> 512 bit multiplication using schoolbook algorithm
// ============================================================================

#ifdef __SIZEOF_INT128__
typedef __uint128_t uint128_t;
#define MUL64(a, b) ((__uint128_t)(a) * (b))
#define LO64(x) ((uint64_t)(x))
#define HI64(x) ((uint64_t)((x) >> 64))

void ecdsa_scalar_mul_512_generic(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t c0 = 0, c1 = 0;
    uint32_t c2 = 0;
    
    // l[0] = a[0] * b[0]
    {
        uint128_t t = MUL64(a->d[0], b->d[0]);
        l[0] = LO64(t);
        c0 = HI64(t);
    }
    
    // l[1] = a[0]*b[1] + a[1]*b[0]
    {
        uint128_t t = MUL64(a->d[0], b->d[1]);
        uint64_t th = HI64(t), tl = LO64(t);
        c0 += tl; th += (c0 < tl); c1 = th;
        
        t = MUL64(a->d[1], b->d[0]);
        th = HI64(t); tl = LO64(t);
        c0 += tl; th += (c0 < tl); c1 += th; c2 = (c1 < th);
        
        l[1] = c0; c0 = c1; c1 = c2; c2 = 0;
    }
    
    // l[2] = a[0]*b[2] + a[1]*b[1] + a[2]*b[0]
    {
        uint128_t t = MUL64(a->d[0], b->d[2]);
        uint64_t th = HI64(t), tl = LO64(t);
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        t = MUL64(a->d[1], b->d[1]);
        th = HI64(t); tl = LO64(t);
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        t = MUL64(a->d[2], b->d[0]);
        th = HI64(t); tl = LO64(t);
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        l[2] = c0; c0 = c1; c1 = c2; c2 = 0;
    }
    
    // l[3] = a[0]*b[3] + a[1]*b[2] + a[2]*b[1] + a[3]*b[0]
    {
        uint128_t t = MUL64(a->d[0], b->d[3]);
        uint64_t th = HI64(t), tl = LO64(t);
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        t = MUL64(a->d[1], b->d[2]);
        th = HI64(t); tl = LO64(t);
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        t = MUL64(a->d[2], b->d[1]);
        th = HI64(t); tl = LO64(t);
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        t = MUL64(a->d[3], b->d[0]);
        th = HI64(t); tl = LO64(t);
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        l[3] = c0; c0 = c1; c1 = c2; c2 = 0;
    }
    
    // l[4] = a[1]*b[3] + a[2]*b[2] + a[3]*b[1]
    {
        uint128_t t = MUL64(a->d[1], b->d[3]);
        uint64_t th = HI64(t), tl = LO64(t);
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        t = MUL64(a->d[2], b->d[2]);
        th = HI64(t); tl = LO64(t);
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        t = MUL64(a->d[3], b->d[1]);
        th = HI64(t); tl = LO64(t);
        c0 += tl; th += (c0 < tl); c1 += th; c2 += (c1 < th);
        
        l[4] = c0; c0 = c1; c1 = c2; c2 = 0;
    }
    
    // l[5] = a[2]*b[3] + a[3]*b[2]
    {
        uint128_t t = MUL64(a->d[2], b->d[3]);
        uint64_t th = HI64(t), tl = LO64(t);
        c0 += tl; th += (c0 < tl); c1 += th;
        
        t = MUL64(a->d[3], b->d[2]);
        th = HI64(t); tl = LO64(t);
        c0 += tl; th += (c0 < tl); c1 += th;
        
        l[5] = c0; c0 = c1; c1 = 0;
    }
    
    // l[6] = a[3]*b[3]
    {
        uint128_t t = MUL64(a->d[3], b->d[3]);
        c0 += LO64(t);
        c1 = HI64(t) + (c0 < LO64(t));
        l[6] = c0;
        l[7] = c1;
    }
}

// ============================================================================
// 512-bit reduction mod n
// ============================================================================

void ecdsa_scalar_reduce_512_generic(ecdsa_scalar_t *r, const uint64_t l[8])
{
    uint64_t r0 = l[0], r1 = l[1], r2 = l[2], r3 = l[3];
    uint64_t h0 = l[4], h1 = l[5], h2 = l[6], h3 = l[7];
    
    // Multiply h by (2^256 mod n) and add to r (simplified)
    if (h0 | h1 | h2 | h3) {
        uint128_t t;
        uint64_t c = 0;
        
        t = MUL64(h0, SCALAR_2P256_MOD_N[0]);
        r0 += LO64(t); c = (r0 < LO64(t));
        t = MUL64(h0, SCALAR_2P256_MOD_N[1]) + c + HI64(t);
        r1 += LO64(t); c = (r1 < LO64(t));
        t = MUL64(h0, SCALAR_2P256_MOD_N[2]) + c + HI64(t);
        r2 += LO64(t); c = (r2 < LO64(t));
        r3 += HI64(t) + c;
    }
    
    // Final reduction
    int over = 0;
    if (r3 > SCALAR_N[3]) over = 1;
    else if (r3 == SCALAR_N[3]) {
        if (r2 > SCALAR_N[2]) over = 1;
        else if (r2 == SCALAR_N[2]) {
            if (r1 > SCALAR_N[1]) over = 1;
            else if (r1 == SCALAR_N[1]) {
                if (r0 >= SCALAR_N[0]) over = 1;
            }
        }
    }
    
    if (over) {
        uint64_t borrow = 0, t;
        t = r0 - SCALAR_N[0]; borrow = (t > r0); r0 = t;
        t = r1 - SCALAR_N[1] - borrow; borrow = (r1 < SCALAR_N[1] + borrow); r1 = t;
        t = r2 - SCALAR_N[2] - borrow; borrow = (r2 < SCALAR_N[2] + borrow); r2 = t;
        r3 = r3 - SCALAR_N[3] - borrow;
    }
    
    r->d[0] = r0; r->d[1] = r1; r->d[2] = r2; r->d[3] = r3;
}

// ============================================================================
// mul_shift_384: (a * b) >> 384 for GLV decomposition
// ============================================================================

void ecdsa_scalar_mul_shift_384_generic(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t l[8];
    ecdsa_scalar_mul_512_generic(l, a, b);
    
    // Result is bits 384-511 = l[6]:l[7] with rounding
    uint64_t round_bit = (l[5] >> 63) & 1;
    r->d[0] = l[6] + round_bit;
    r->d[1] = l[7] + (r->d[0] < round_bit);
    r->d[2] = 0;
    r->d[3] = 0;
}

// ============================================================================
// Full scalar multiplication: (a * b) mod n
// ============================================================================

void ecdsa_scalar_mul_generic(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t l[8];
    ecdsa_scalar_mul_512_generic(l, a, b);
    ecdsa_scalar_reduce_512_generic(r, l);
}

#else
#error "Generic implementation requires __uint128_t"
#endif
