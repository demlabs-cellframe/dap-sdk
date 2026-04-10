#include <stdint.h>
#include "dilithium_rounding_reduce.h"

/*************************************************/
uint32_t montgomery_reduce(uint64_t a)
{
    uint64_t t;

    t = a * QINV;
    t &= (1ULL << 32) - 1;
    t *= Q;
    t = a + t;
    t >>= 32;
    return t;
}

/*************************************************/
uint32_t reduce32(uint32_t a)
{
    uint32_t t;

    t = a & 0x7FFFFF;
    a >>= 23;
    t += (a << 13) - a;
    return t;
}

/*************************************************/
uint32_t csubq(uint32_t a)
{
    a -= Q;
    a += ((int32_t)a >> 31) & Q;
    return a;
}

/*************************************************/
uint32_t freeze(uint32_t a)
{
    a = reduce32(a);
    a = csubq(a);
    return a;
}

/*************************************************/
uint32_t power2round_p(uint32_t a, uint32_t *a0, const dilithium_param_t *p)
{
    uint32_t d_val = dil_d(p);
    int32_t t;
    t = a & ((1 << d_val) - 1);
    t -= (1 << (d_val-1)) + 1;
    t += (t >> 31) & (1 << d_val);
    t -= (1 << (d_val-1)) - 1;
    *a0 = Q + t;
    return (a - t) >> d_val;
}

/*************************************************/
uint32_t decompose_p(uint32_t a, uint32_t *a0, const dilithium_param_t *p)
{
    int32_t a1;
    uint32_t gamma2 = dil_gamma2(p);

    a1 = (a + 127) >> 7;

    if (gamma2 == (Q - 1) / 32) {
        a1 = (a1 * 1025 + (1 << 21)) >> 22;
        a1 &= 15;
    } else {
        /* gamma2 == (Q - 1) / 88 */
        a1 = (a1 * 11275 + (1 << 23)) >> 24;
        a1 ^= ((43 - a1) >> 31) & a1;
    }

    /* Compute centered remainder, store as Q + centered for our convention */
    int32_t r0 = (int32_t)a - a1 * (int32_t)(2 * gamma2);
    r0 -= (((int32_t)(Q - 1) / 2 - r0) >> 31) & (int32_t)Q;
    *a0 = (uint32_t)((int32_t)Q + r0);
    return (uint32_t)a1;
}

/*************************************************/
unsigned int make_hint_p(uint32_t a, uint32_t b, const dilithium_param_t *p)
{
    uint32_t t;
    return decompose_p(a, &t, p) != decompose_p(b, &t, p);
}

/*************************************************/
uint32_t use_hint_p(uint32_t a, unsigned int hint, const dilithium_param_t *p)
{
    uint32_t a0, a1;
    uint32_t gamma2 = dil_gamma2(p);

    a1 = decompose_p(a, &a0, p);
    if (hint == 0)
        return a1;

    /* a0 stored as Q + centered: a0 > Q means positive centered value */
    if (gamma2 == (Q - 1) / 32) {
        if (a0 > Q)
            return (a1 + 1) & 15;
        else
            return (a1 - 1) & 15;
    } else {
        /* gamma2 == (Q - 1) / 88 */
        if (a0 > Q)
            return (a1 == 43) ? 0 : a1 + 1;
        else
            return (a1 == 0) ? 43 : a1 - 1;
    }
}

