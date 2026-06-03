#ifndef REDUCE_H
#define REDUCE_H

#include <stdint.h>
#include "dilithium_params.h"

#define MONT 4193792U
#define QINV 4236238847U

uint32_t montgomery_reduce(uint64_t a);

uint32_t reduce32(uint32_t a);

uint32_t csubq(uint32_t a);

uint32_t freeze(uint32_t a);

uint32_t power2round_p(uint32_t a, uint32_t *a0, const dilithium_param_t *p);
uint32_t decompose_p(uint32_t a, uint32_t *a0, const dilithium_param_t *p);
unsigned int make_hint_p(uint32_t a, uint32_t b, const dilithium_param_t *p);
uint32_t use_hint_p(uint32_t a, unsigned int hint, const dilithium_param_t *p);

/* Legacy wrappers for backward compatibility */
static inline uint32_t power2round(uint32_t a, uint32_t *a0) {
    int32_t t;
    t = a & ((1 << D) - 1);
    t -= (1 << (D-1)) + 1;
    t += (t >> 31) & (1 << D);
    t -= (1 << (D-1)) - 1;
    *a0 = Q + t;
    return (a - t) >> D;
}
static inline uint32_t decompose(uint32_t a, uint32_t *a0) {
    int32_t t, u;
    t = a & 0x7FFFF;
    t += (a >> 19) << 9;
    t -= ALPHA/2 + 1;
    t += (t >> 31) & ALPHA;
    t -= ALPHA/2 - 1;
    a -= t;
    u = a - 1;
    u >>= 31;
    a = (a >> 19) + 1;
    a -= u & 1;
    *a0 = Q + t - (a >> 4);
    a &= 0xF;
    return a;
}
static inline unsigned int make_hint(uint32_t a, uint32_t b) {
    uint32_t t;
    return decompose(a, &t) != decompose(b, &t);
}
static inline uint32_t use_hint(uint32_t a, unsigned int hint) {
    uint32_t a0, a1;
    a1 = decompose(a, &a0);
    if (hint == 0) return a1;
    if (a0 > Q) return (a1 + 1) & 0xF;
    return (a1 - 1) & 0xF;
}

#endif
