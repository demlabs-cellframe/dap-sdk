/*
 * Internal ECDSA field arithmetic implementation (mod p)
 * 
 * secp256k1 prime: p = 2^256 - 2^32 - 977
 * 
 * Uses 5x52-bit limb representation on 64-bit platforms,
 * or 10x26-bit limb representation on 32-bit platforms.
 */

#include "ecdsa_field.h"
#include <string.h>

// =============================================================================
// Architecture-specific implementations
// =============================================================================

#ifdef ECDSA_FIELD_52BIT

// -----------------------------------------------------------------------------
// 64-bit: 5x52-bit limb representation
// -----------------------------------------------------------------------------

#define ECDSA_M52 0xFFFFFFFFFFFFFULL
#define ECDSA_M48 0xFFFFFFFFFFFFULL

const ecdsa_field_t ECDSA_FIELD_P = {{
    0xFFFFEFFFFFC2FULL, 0xFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFULL,
    0xFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFULL
}};
const ecdsa_field_t ECDSA_FIELD_ZERO = {{0, 0, 0, 0, 0}};
const ecdsa_field_t ECDSA_FIELD_ONE = {{1, 0, 0, 0, 0}};

void ecdsa_field_normalize(ecdsa_field_t *r) {
    uint64_t t0 = r->n[0], t1 = r->n[1], t2 = r->n[2], t3 = r->n[3], t4 = r->n[4];
    uint64_t m;
    
    t1 += t0 >> 52; t0 &= ECDSA_M52;
    t2 += t1 >> 52; t1 &= ECDSA_M52;
    t3 += t2 >> 52; t2 &= ECDSA_M52;
    t4 += t3 >> 52; t3 &= ECDSA_M52;
    
    m = t4 >> 48;
    t4 &= ECDSA_M48;
    t0 += m * 0x1000003D1ULL;
    
    t1 += t0 >> 52; t0 &= ECDSA_M52;
    t2 += t1 >> 52; t1 &= ECDSA_M52;
    t3 += t2 >> 52; t2 &= ECDSA_M52;
    t4 += t3 >> 52; t3 &= ECDSA_M52;
    
    m = (t4 == ECDSA_M48) & (t3 == ECDSA_M52) & (t2 == ECDSA_M52) & 
        (t1 == ECDSA_M52) & (t0 >= 0xFFFFEFFFFFC2FULL);
    if (m) {
        t0 -= 0xFFFFEFFFFFC2FULL;
        t1 -= ECDSA_M52 + (t0 >> 63); t0 &= ECDSA_M52;
        t2 -= ECDSA_M52 + (t1 >> 63); t1 &= ECDSA_M52;
        t3 -= ECDSA_M52 + (t2 >> 63); t2 &= ECDSA_M52;
        t4 -= ECDSA_M48 + (t3 >> 63); t3 &= ECDSA_M52;
    }
    
    r->n[0] = t0; r->n[1] = t1; r->n[2] = t2; r->n[3] = t3; r->n[4] = t4;
}

void ecdsa_field_normalize_weak(ecdsa_field_t *r) {
    uint64_t t0 = r->n[0], t1 = r->n[1], t2 = r->n[2], t3 = r->n[3], t4 = r->n[4];
    
    t1 += t0 >> 52; t0 &= ECDSA_M52;
    t2 += t1 >> 52; t1 &= ECDSA_M52;
    t3 += t2 >> 52; t2 &= ECDSA_M52;
    t4 += t3 >> 52; t3 &= ECDSA_M52;
    
    uint64_t m = t4 >> 48;
    t4 &= ECDSA_M48;
    t0 += m * 0x1000003D1ULL;
    
    t1 += t0 >> 52; t0 &= ECDSA_M52;
    t2 += t1 >> 52; t1 &= ECDSA_M52;
    t3 += t2 >> 52; t2 &= ECDSA_M52;
    t4 += t3 >> 52; t3 &= ECDSA_M52;
    
    r->n[0] = t0; r->n[1] = t1; r->n[2] = t2; r->n[3] = t3; r->n[4] = t4;
}

bool ecdsa_field_set_b32(ecdsa_field_t *r, const uint8_t *a) {
    r->n[0] = (uint64_t)a[31] | ((uint64_t)a[30] << 8) | ((uint64_t)a[29] << 16) |
              ((uint64_t)a[28] << 24) | ((uint64_t)a[27] << 32) | ((uint64_t)a[26] << 40) |
              ((uint64_t)(a[25] & 0xF) << 48);
    r->n[1] = (uint64_t)(a[25] >> 4) | ((uint64_t)a[24] << 4) | ((uint64_t)a[23] << 12) |
              ((uint64_t)a[22] << 20) | ((uint64_t)a[21] << 28) | ((uint64_t)a[20] << 36) |
              ((uint64_t)a[19] << 44);
    r->n[2] = (uint64_t)a[18] | ((uint64_t)a[17] << 8) | ((uint64_t)a[16] << 16) |
              ((uint64_t)a[15] << 24) | ((uint64_t)a[14] << 32) | ((uint64_t)a[13] << 40) |
              ((uint64_t)(a[12] & 0xF) << 48);
    r->n[3] = (uint64_t)(a[12] >> 4) | ((uint64_t)a[11] << 4) | ((uint64_t)a[10] << 12) |
              ((uint64_t)a[9] << 20) | ((uint64_t)a[8] << 28) | ((uint64_t)a[7] << 36) |
              ((uint64_t)a[6] << 44);
    r->n[4] = (uint64_t)a[5] | ((uint64_t)a[4] << 8) | ((uint64_t)a[3] << 16) |
              ((uint64_t)a[2] << 24) | ((uint64_t)a[1] << 32) | ((uint64_t)a[0] << 40);
    
    bool overflow = (r->n[4] > ECDSA_M48) ||
                    ((r->n[4] == ECDSA_M48) && (r->n[3] == ECDSA_M52) && (r->n[2] == ECDSA_M52) && 
                     (r->n[1] == ECDSA_M52) && (r->n[0] >= 0xFFFFFEFFFFFC2FULL));
    ecdsa_field_normalize(r);
    return !overflow;
}

void ecdsa_field_get_b32(uint8_t *r, const ecdsa_field_t *a) {
    r[31] = a->n[0] & 0xFF;
    r[30] = (a->n[0] >> 8) & 0xFF;
    r[29] = (a->n[0] >> 16) & 0xFF;
    r[28] = (a->n[0] >> 24) & 0xFF;
    r[27] = (a->n[0] >> 32) & 0xFF;
    r[26] = (a->n[0] >> 40) & 0xFF;
    r[25] = ((a->n[0] >> 48) & 0xF) | ((a->n[1] & 0xF) << 4);
    r[24] = (a->n[1] >> 4) & 0xFF;
    r[23] = (a->n[1] >> 12) & 0xFF;
    r[22] = (a->n[1] >> 20) & 0xFF;
    r[21] = (a->n[1] >> 28) & 0xFF;
    r[20] = (a->n[1] >> 36) & 0xFF;
    r[19] = (a->n[1] >> 44) & 0xFF;
    r[18] = a->n[2] & 0xFF;
    r[17] = (a->n[2] >> 8) & 0xFF;
    r[16] = (a->n[2] >> 16) & 0xFF;
    r[15] = (a->n[2] >> 24) & 0xFF;
    r[14] = (a->n[2] >> 32) & 0xFF;
    r[13] = (a->n[2] >> 40) & 0xFF;
    r[12] = ((a->n[2] >> 48) & 0xF) | ((a->n[3] & 0xF) << 4);
    r[11] = (a->n[3] >> 4) & 0xFF;
    r[10] = (a->n[3] >> 12) & 0xFF;
    r[9] = (a->n[3] >> 20) & 0xFF;
    r[8] = (a->n[3] >> 28) & 0xFF;
    r[7] = (a->n[3] >> 36) & 0xFF;
    r[6] = (a->n[3] >> 44) & 0xFF;
    r[5] = a->n[4] & 0xFF;
    r[4] = (a->n[4] >> 8) & 0xFF;
    r[3] = (a->n[4] >> 16) & 0xFF;
    r[2] = (a->n[4] >> 24) & 0xFF;
    r[1] = (a->n[4] >> 32) & 0xFF;
    r[0] = (a->n[4] >> 40) & 0xFF;
}

void ecdsa_field_negate(ecdsa_field_t *r, const ecdsa_field_t *a, int m) {
    (void)m;
    uint64_t t0 = 0xFFFFFEFFFFFC2FULL - a->n[0];
    uint64_t t1 = ECDSA_M52 - a->n[1] - (t0 >> 63); t0 &= ECDSA_M52;
    uint64_t t2 = ECDSA_M52 - a->n[2] - (t1 >> 63); t1 &= ECDSA_M52;
    uint64_t t3 = ECDSA_M52 - a->n[3] - (t2 >> 63); t2 &= ECDSA_M52;
    uint64_t t4 = ECDSA_M48 - a->n[4] - (t3 >> 63); t3 &= ECDSA_M52;
    r->n[0] = t0; r->n[1] = t1; r->n[2] = t2; r->n[3] = t3; r->n[4] = t4;
}

void ecdsa_field_add(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b) {
    r->n[0] = a->n[0] + b->n[0];
    r->n[1] = a->n[1] + b->n[1];
    r->n[2] = a->n[2] + b->n[2];
    r->n[3] = a->n[3] + b->n[3];
    r->n[4] = a->n[4] + b->n[4];
    ecdsa_field_normalize_weak(r);
}

void ecdsa_field_mul(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b) {
#ifdef __SIZEOF_INT128__
    __uint128_t t[10] = {0};
    __uint128_t c;
    
    uint64_t an[5] = {a->n[0], a->n[1], a->n[2], a->n[3], a->n[4]};
    uint64_t bn[5] = {b->n[0], b->n[1], b->n[2], b->n[3], b->n[4]};
    
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 5; j++)
            t[i+j] += (__uint128_t)an[i] * bn[j];
    
    for (int i = 0; i < 9; i++) {
        t[i+1] += t[i] >> 52;
        t[i] &= ECDSA_M52;
    }
    
    const uint64_t R = 0x1000003D1ULL;
    
    c = t[5] * R * 16;
    t[0] += c & ECDSA_M52; c >>= 52;
    t[1] += c & ECDSA_M52; c >>= 52;
    t[2] += c;
    
    c = t[6] * R * 16;
    t[1] += c & ECDSA_M52; c >>= 52;
    t[2] += c & ECDSA_M52; c >>= 52;
    t[3] += c;
    
    c = t[7] * R * 16;
    t[2] += c & ECDSA_M52; c >>= 52;
    t[3] += c & ECDSA_M52; c >>= 52;
    t[4] += c;
    
    c = t[8] * R * 16;
    t[3] += c & ECDSA_M52; c >>= 52;
    t[4] += c;
    
    t[4] += t[9] * R * 16;
    
    for (int pass = 0; pass < 3; pass++) {
        c = 0;
        for (int i = 0; i < 4; i++) {
            c += t[i];
            t[i] = c & ECDSA_M52;
            c >>= 52;
        }
        t[4] += c;
        if (t[4] >> 48) {
            __uint128_t overflow = t[4] >> 48;
            t[4] &= ECDSA_M48;
            t[0] += overflow * R;
        }
    }
    
    r->n[0] = (uint64_t)t[0]; 
    r->n[1] = (uint64_t)t[1]; 
    r->n[2] = (uint64_t)t[2]; 
    r->n[3] = (uint64_t)t[3]; 
    r->n[4] = (uint64_t)t[4];
    ecdsa_field_normalize_weak(r);
#else
    // Fallback without __int128
    uint64_t t[10] = {0};
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5; j++) {
            __uint128_t p = (__uint128_t)a->n[i] * b->n[j];
            t[i+j] += (uint64_t)p;
            if (i+j+1 < 10) t[i+j+1] += (uint64_t)(p >> 64);
        }
    }
    const uint64_t R = 0x1000003D1ULL;
    for (int i = 5; i < 10; i++) {
        t[i-5] += t[i] * R;
    }
    r->n[0] = t[0] & ECDSA_M52;
    r->n[1] = (t[1] + (t[0] >> 52)) & ECDSA_M52;
    r->n[2] = (t[2] + (t[1] >> 52)) & ECDSA_M52;
    r->n[3] = (t[3] + (t[2] >> 52)) & ECDSA_M52;
    r->n[4] = t[4] + (t[3] >> 52);
    ecdsa_field_normalize_weak(r);
#endif
}

void ecdsa_field_sqr(ecdsa_field_t *r, const ecdsa_field_t *a) {
#ifdef __SIZEOF_INT128__
    const uint64_t R = 0x1000003D1ULL;
    uint64_t an[5] = {a->n[0], a->n[1], a->n[2], a->n[3], a->n[4]};
    __uint128_t t[10] = {0};
    
    // Diagonal
    t[0] = (__uint128_t)an[0] * an[0];
    t[2] = (__uint128_t)an[1] * an[1];
    t[4] = (__uint128_t)an[2] * an[2];
    t[6] = (__uint128_t)an[3] * an[3];
    t[8] = (__uint128_t)an[4] * an[4];
    
    // Off-diagonal (doubled)
    __uint128_t d;
    d = (__uint128_t)an[0] * an[1]; t[1] += d * 2;
    d = (__uint128_t)an[0] * an[2]; t[2] += d * 2;
    d = (__uint128_t)an[0] * an[3]; t[3] += d * 2;
    d = (__uint128_t)an[0] * an[4]; t[4] += d * 2;
    d = (__uint128_t)an[1] * an[2]; t[3] += d * 2;
    d = (__uint128_t)an[1] * an[3]; t[4] += d * 2;
    d = (__uint128_t)an[1] * an[4]; t[5] += d * 2;
    d = (__uint128_t)an[2] * an[3]; t[5] += d * 2;
    d = (__uint128_t)an[2] * an[4]; t[6] += d * 2;
    d = (__uint128_t)an[3] * an[4]; t[7] += d * 2;
    
    // Propagate and reduce
    for (int i = 0; i < 9; i++) {
        t[i+1] += t[i] >> 52;
        t[i] &= ECDSA_M52;
    }
    
    __uint128_t c;
    c = t[5] * R * 16; t[0] += c; c = t[0] >> 52; t[0] &= ECDSA_M52; t[1] += c;
    c = t[6] * R * 16; t[1] += c; c = t[1] >> 52; t[1] &= ECDSA_M52; t[2] += c;
    c = t[7] * R * 16; t[2] += c; c = t[2] >> 52; t[2] &= ECDSA_M52; t[3] += c;
    c = t[8] * R * 16; t[3] += c; c = t[3] >> 52; t[3] &= ECDSA_M52; t[4] += c;
    t[4] += t[9] * R * 16;
    
    c = t[4] >> 48; t[4] &= ECDSA_M48; t[0] += c * R;
    t[1] += t[0] >> 52; t[0] &= ECDSA_M52;
    t[2] += t[1] >> 52; t[1] &= ECDSA_M52;
    t[3] += t[2] >> 52; t[2] &= ECDSA_M52;
    t[4] += t[3] >> 52; t[3] &= ECDSA_M52;
    
    if (t[4] >> 48) {
        c = t[4] >> 48; t[4] &= ECDSA_M48; t[0] += c * R;
        t[1] += t[0] >> 52; t[0] &= ECDSA_M52;
        t[2] += t[1] >> 52; t[1] &= ECDSA_M52;
        t[3] += t[2] >> 52; t[2] &= ECDSA_M52;
        t[4] += t[3] >> 52; t[3] &= ECDSA_M52;
    }
    
    r->n[0] = (uint64_t)t[0];
    r->n[1] = (uint64_t)t[1];
    r->n[2] = (uint64_t)t[2];
    r->n[3] = (uint64_t)t[3];
    r->n[4] = (uint64_t)t[4];
#else
    ecdsa_field_mul(r, a, a);
#endif
}

#else // ECDSA_FIELD_26BIT

// -----------------------------------------------------------------------------
// 32-bit: 10x26-bit limb representation
// -----------------------------------------------------------------------------

#define ECDSA_M26 0x3FFFFFFUL

const ecdsa_field_t ECDSA_FIELD_P = {{
    0x3FFFC2FUL, 0x3FFFFBFUL, 0x3FFFFFFUL, 0x3FFFFFFUL, 0x3FFFFFFUL,
    0x3FFFFFFUL, 0x3FFFFFFUL, 0x3FFFFFFUL, 0x3FFFFFFUL, 0x03FFFFFUL
}};
const ecdsa_field_t ECDSA_FIELD_ZERO = {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};
const ecdsa_field_t ECDSA_FIELD_ONE = {{1, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

void ecdsa_field_normalize(ecdsa_field_t *r) {
    uint32_t t0 = r->n[0], t1 = r->n[1], t2 = r->n[2], t3 = r->n[3], t4 = r->n[4];
    uint32_t t5 = r->n[5], t6 = r->n[6], t7 = r->n[7], t8 = r->n[8], t9 = r->n[9];
    uint32_t m;
    
    t1 += t0 >> 26; t0 &= ECDSA_M26;
    t2 += t1 >> 26; t1 &= ECDSA_M26;
    t3 += t2 >> 26; t2 &= ECDSA_M26;
    t4 += t3 >> 26; t3 &= ECDSA_M26;
    t5 += t4 >> 26; t4 &= ECDSA_M26;
    t6 += t5 >> 26; t5 &= ECDSA_M26;
    t7 += t6 >> 26; t6 &= ECDSA_M26;
    t8 += t7 >> 26; t7 &= ECDSA_M26;
    t9 += t8 >> 26; t8 &= ECDSA_M26;
    
    m = t9 >> 22;
    t9 &= 0x3FFFFFUL;
    t0 += m * 0x3D1UL;
    t1 += m * 0x40UL;
    
    t1 += t0 >> 26; t0 &= ECDSA_M26;
    t2 += t1 >> 26; t1 &= ECDSA_M26;
    t3 += t2 >> 26; t2 &= ECDSA_M26;
    t4 += t3 >> 26; t3 &= ECDSA_M26;
    t5 += t4 >> 26; t4 &= ECDSA_M26;
    t6 += t5 >> 26; t5 &= ECDSA_M26;
    t7 += t6 >> 26; t6 &= ECDSA_M26;
    t8 += t7 >> 26; t7 &= ECDSA_M26;
    t9 += t8 >> 26; t8 &= ECDSA_M26;
    
    m = (t9 == 0x3FFFFFUL) & (t8 == ECDSA_M26) & (t7 == ECDSA_M26) & (t6 == ECDSA_M26) &
        (t5 == ECDSA_M26) & (t4 == ECDSA_M26) & (t3 == ECDSA_M26) & (t2 == ECDSA_M26) &
        (t1 >= 0x3FFFFBFUL) & (t0 >= 0x3FFFC2FUL);
    
    if (m) {
        t0 -= 0x3FFFC2FUL;
        t1 -= 0x3FFFFBFUL + (t0 >> 31); t0 &= ECDSA_M26;
        t2 -= ECDSA_M26 + (t1 >> 31); t1 &= ECDSA_M26;
        t3 -= ECDSA_M26 + (t2 >> 31); t2 &= ECDSA_M26;
        t4 -= ECDSA_M26 + (t3 >> 31); t3 &= ECDSA_M26;
        t5 -= ECDSA_M26 + (t4 >> 31); t4 &= ECDSA_M26;
        t6 -= ECDSA_M26 + (t5 >> 31); t5 &= ECDSA_M26;
        t7 -= ECDSA_M26 + (t6 >> 31); t6 &= ECDSA_M26;
        t8 -= ECDSA_M26 + (t7 >> 31); t7 &= ECDSA_M26;
        t9 -= 0x3FFFFFUL + (t8 >> 31); t8 &= ECDSA_M26;
    }
    
    r->n[0] = t0; r->n[1] = t1; r->n[2] = t2; r->n[3] = t3; r->n[4] = t4;
    r->n[5] = t5; r->n[6] = t6; r->n[7] = t7; r->n[8] = t8; r->n[9] = t9;
}

void ecdsa_field_normalize_weak(ecdsa_field_t *r) {
    uint32_t t0 = r->n[0], t1 = r->n[1], t2 = r->n[2], t3 = r->n[3], t4 = r->n[4];
    uint32_t t5 = r->n[5], t6 = r->n[6], t7 = r->n[7], t8 = r->n[8], t9 = r->n[9];
    
    t1 += t0 >> 26; t0 &= ECDSA_M26;
    t2 += t1 >> 26; t1 &= ECDSA_M26;
    t3 += t2 >> 26; t2 &= ECDSA_M26;
    t4 += t3 >> 26; t3 &= ECDSA_M26;
    t5 += t4 >> 26; t4 &= ECDSA_M26;
    t6 += t5 >> 26; t5 &= ECDSA_M26;
    t7 += t6 >> 26; t6 &= ECDSA_M26;
    t8 += t7 >> 26; t7 &= ECDSA_M26;
    t9 += t8 >> 26; t8 &= ECDSA_M26;
    
    uint32_t m = t9 >> 22;
    t9 &= 0x3FFFFFUL;
    t0 += m * 0x3D1UL;
    t1 += m * 0x40UL;
    
    t1 += t0 >> 26; t0 &= ECDSA_M26;
    t2 += t1 >> 26; t1 &= ECDSA_M26;
    t3 += t2 >> 26; t2 &= ECDSA_M26;
    t4 += t3 >> 26; t3 &= ECDSA_M26;
    t5 += t4 >> 26; t4 &= ECDSA_M26;
    t6 += t5 >> 26; t5 &= ECDSA_M26;
    t7 += t6 >> 26; t6 &= ECDSA_M26;
    t8 += t7 >> 26; t7 &= ECDSA_M26;
    t9 += t8 >> 26; t8 &= ECDSA_M26;
    
    r->n[0] = t0; r->n[1] = t1; r->n[2] = t2; r->n[3] = t3; r->n[4] = t4;
    r->n[5] = t5; r->n[6] = t6; r->n[7] = t7; r->n[8] = t8; r->n[9] = t9;
}

bool ecdsa_field_set_b32(ecdsa_field_t *r, const uint8_t *a) {
    r->n[0] = (uint32_t)a[31] | ((uint32_t)a[30] << 8) | ((uint32_t)a[29] << 16) | ((uint32_t)(a[28] & 0x3) << 24);
    r->n[1] = (uint32_t)(a[28] >> 2) | ((uint32_t)a[27] << 6) | ((uint32_t)a[26] << 14) | ((uint32_t)(a[25] & 0xF) << 22);
    r->n[2] = (uint32_t)(a[25] >> 4) | ((uint32_t)a[24] << 4) | ((uint32_t)a[23] << 12) | ((uint32_t)(a[22] & 0x3F) << 20);
    r->n[3] = (uint32_t)(a[22] >> 6) | ((uint32_t)a[21] << 2) | ((uint32_t)a[20] << 10) | ((uint32_t)a[19] << 18);
    r->n[4] = (uint32_t)a[18] | ((uint32_t)a[17] << 8) | ((uint32_t)a[16] << 16) | ((uint32_t)(a[15] & 0x3) << 24);
    r->n[5] = (uint32_t)(a[15] >> 2) | ((uint32_t)a[14] << 6) | ((uint32_t)a[13] << 14) | ((uint32_t)(a[12] & 0xF) << 22);
    r->n[6] = (uint32_t)(a[12] >> 4) | ((uint32_t)a[11] << 4) | ((uint32_t)a[10] << 12) | ((uint32_t)(a[9] & 0x3F) << 20);
    r->n[7] = (uint32_t)(a[9] >> 6) | ((uint32_t)a[8] << 2) | ((uint32_t)a[7] << 10) | ((uint32_t)a[6] << 18);
    r->n[8] = (uint32_t)a[5] | ((uint32_t)a[4] << 8) | ((uint32_t)a[3] << 16) | ((uint32_t)(a[2] & 0x3) << 24);
    r->n[9] = (uint32_t)(a[2] >> 2) | ((uint32_t)a[1] << 6) | ((uint32_t)a[0] << 14);
    ecdsa_field_normalize(r);
    return true;
}

void ecdsa_field_get_b32(uint8_t *r, const ecdsa_field_t *a) {
    r[31] = a->n[0] & 0xFF;
    r[30] = (a->n[0] >> 8) & 0xFF;
    r[29] = (a->n[0] >> 16) & 0xFF;
    r[28] = ((a->n[0] >> 24) & 0x3) | ((a->n[1] & 0x3F) << 2);
    r[27] = (a->n[1] >> 6) & 0xFF;
    r[26] = (a->n[1] >> 14) & 0xFF;
    r[25] = ((a->n[1] >> 22) & 0xF) | ((a->n[2] & 0xF) << 4);
    r[24] = (a->n[2] >> 4) & 0xFF;
    r[23] = (a->n[2] >> 12) & 0xFF;
    r[22] = ((a->n[2] >> 20) & 0x3F) | ((a->n[3] & 0x3) << 6);
    r[21] = (a->n[3] >> 2) & 0xFF;
    r[20] = (a->n[3] >> 10) & 0xFF;
    r[19] = (a->n[3] >> 18) & 0xFF;
    r[18] = a->n[4] & 0xFF;
    r[17] = (a->n[4] >> 8) & 0xFF;
    r[16] = (a->n[4] >> 16) & 0xFF;
    r[15] = ((a->n[4] >> 24) & 0x3) | ((a->n[5] & 0x3F) << 2);
    r[14] = (a->n[5] >> 6) & 0xFF;
    r[13] = (a->n[5] >> 14) & 0xFF;
    r[12] = ((a->n[5] >> 22) & 0xF) | ((a->n[6] & 0xF) << 4);
    r[11] = (a->n[6] >> 4) & 0xFF;
    r[10] = (a->n[6] >> 12) & 0xFF;
    r[9] = ((a->n[6] >> 20) & 0x3F) | ((a->n[7] & 0x3) << 6);
    r[8] = (a->n[7] >> 2) & 0xFF;
    r[7] = (a->n[7] >> 10) & 0xFF;
    r[6] = (a->n[7] >> 18) & 0xFF;
    r[5] = a->n[8] & 0xFF;
    r[4] = (a->n[8] >> 8) & 0xFF;
    r[3] = (a->n[8] >> 16) & 0xFF;
    r[2] = ((a->n[8] >> 24) & 0x3) | ((a->n[9] & 0x3F) << 2);
    r[1] = (a->n[9] >> 6) & 0xFF;
    r[0] = (a->n[9] >> 14) & 0xFF;
}

void ecdsa_field_negate(ecdsa_field_t *r, const ecdsa_field_t *a, int m) {
    (void)m;
    r->n[0] = 0x3FFFC2FUL * 2 - a->n[0];
    r->n[1] = 0x3FFFFBFUL * 2 - a->n[1];
    r->n[2] = ECDSA_M26 * 2 - a->n[2];
    r->n[3] = ECDSA_M26 * 2 - a->n[3];
    r->n[4] = ECDSA_M26 * 2 - a->n[4];
    r->n[5] = ECDSA_M26 * 2 - a->n[5];
    r->n[6] = ECDSA_M26 * 2 - a->n[6];
    r->n[7] = ECDSA_M26 * 2 - a->n[7];
    r->n[8] = ECDSA_M26 * 2 - a->n[8];
    r->n[9] = 0x3FFFFFUL * 2 - a->n[9];
}

void ecdsa_field_add(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b) {
    for (int i = 0; i < 10; i++)
        r->n[i] = a->n[i] + b->n[i];
    ecdsa_field_normalize_weak(r);
}

void ecdsa_field_mul(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b) {
    uint64_t c;
    uint64_t t[19] = {0};
    uint32_t an[10], bn[10];
    
    for (int i = 0; i < 10; i++) {
        an[i] = a->n[i];
        bn[i] = b->n[i];
    }
    
    for (int i = 0; i < 10; i++)
        for (int j = 0; j < 10; j++)
            t[i+j] += (uint64_t)an[i] * bn[j];
    
    const uint32_t R0 = 0x3D1, R1 = 0x40;
    
    for (int i = 10; i < 19; i++) {
        uint64_t v = t[i];
        t[i-10] += (v & ECDSA_M26) * R0 + ((v >> 26) & ECDSA_M26) * R0 * 64;
        t[i-9] += (v & ECDSA_M26) * R1;
    }
    
    c = 0;
    for (int i = 0; i < 9; i++) {
        c += t[i];
        t[i] = c & ECDSA_M26;
        c >>= 26;
    }
    t[9] += c;
    
    uint64_t d = t[9] >> 22;
    t[9] &= 0x3FFFFFUL;
    t[0] += d * R0;
    t[1] += d * R1;
    
    c = 0;
    for (int i = 0; i < 9; i++) {
        c += t[i];
        r->n[i] = (uint32_t)(c & ECDSA_M26);
        c >>= 26;
    }
    r->n[9] = (uint32_t)(t[9] + c);
    
    ecdsa_field_normalize_weak(r);
}

void ecdsa_field_sqr(ecdsa_field_t *r, const ecdsa_field_t *a) {
    ecdsa_field_mul(r, a, a);
}

#endif // ECDSA_FIELD_52BIT

// =============================================================================
// Common functions (architecture-independent)
// =============================================================================

void ecdsa_field_clear(ecdsa_field_t *r) {
    memset(r->n, 0, sizeof(r->n));
}

void ecdsa_field_set_int(ecdsa_field_t *r, int a) {
    ecdsa_field_clear(r);
    r->n[0] = (ecdsa_field_limb_t)(a >= 0 ? a : 0);
}

void ecdsa_field_copy(ecdsa_field_t *r, const ecdsa_field_t *a) {
    *r = *a;
}

bool ecdsa_field_is_zero(const ecdsa_field_t *a) {
    ecdsa_field_limb_t z = 0;
    for (int i = 0; i < ECDSA_FIELD_LIMBS; i++)
        z |= a->n[i];
    return z == 0;
}

bool ecdsa_field_is_odd(const ecdsa_field_t *a) {
    return a->n[0] & 1;
}

bool ecdsa_field_equal(const ecdsa_field_t *a, const ecdsa_field_t *b) {
    ecdsa_field_limb_t diff = 0;
    for (int i = 0; i < ECDSA_FIELD_LIMBS; i++)
        diff |= a->n[i] ^ b->n[i];
    return diff == 0;
}

// =============================================================================
// Modular inverse: a^(-1) = a^(p-2) mod p (Fermat's Little Theorem)
// Optimized addition chain: ~258 squarings + ~15 multiplications
// =============================================================================

void ecdsa_field_inv(ecdsa_field_t *r, const ecdsa_field_t *a) {
    ecdsa_field_t x2, x3, x6, x9, x11, x22, x44, x88, x176, x220, x223, t;
    
    ecdsa_field_sqr(&x2, a);
    ecdsa_field_mul(&x2, &x2, a);
    
    ecdsa_field_sqr(&x3, &x2);
    ecdsa_field_mul(&x3, &x3, a);
    
    ecdsa_field_sqr(&t, &x3);
    for (int i = 1; i < 3; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x6, &t, &x3);
    
    ecdsa_field_sqr(&t, &x6);
    for (int i = 1; i < 3; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x9, &t, &x3);
    
    ecdsa_field_sqr(&t, &x9);
    ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x11, &t, &x2);
    
    ecdsa_field_sqr(&t, &x11);
    for (int i = 1; i < 11; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x22, &t, &x11);
    
    ecdsa_field_sqr(&t, &x22);
    for (int i = 1; i < 22; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x44, &t, &x22);
    
    ecdsa_field_sqr(&t, &x44);
    for (int i = 1; i < 44; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x88, &t, &x44);
    
    ecdsa_field_sqr(&t, &x88);
    for (int i = 1; i < 88; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x176, &t, &x88);
    
    ecdsa_field_sqr(&t, &x176);
    for (int i = 1; i < 44; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x220, &t, &x44);
    
    ecdsa_field_sqr(&t, &x220);
    for (int i = 1; i < 3; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x223, &t, &x3);
    
    ecdsa_field_sqr(&t, &x223);
    for (int i = 1; i < 23; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&t, &t, &x22);
    
    for (int i = 0; i < 5; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&t, &t, a);
    
    for (int i = 0; i < 3; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&t, &t, &x2);
    
    ecdsa_field_sqr(&t, &t);
    ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(r, &t, a);
    
    ecdsa_field_normalize(r);
}

// =============================================================================
// Square root: sqrt(a) = a^((p+1)/4) mod p (since p ≡ 3 mod 4)
// =============================================================================

bool ecdsa_field_sqrt(ecdsa_field_t *r, const ecdsa_field_t *a) {
    ecdsa_field_t x2, x3, x6, x9, x11, x22, x44, x88, x176, x220, x223, t;
    
    ecdsa_field_sqr(&x2, a);
    ecdsa_field_mul(&x2, &x2, a);
    
    ecdsa_field_sqr(&x3, &x2);
    ecdsa_field_mul(&x3, &x3, a);
    
    ecdsa_field_sqr(&t, &x3);
    for (int i = 0; i < 2; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x6, &t, &x3);
    
    ecdsa_field_sqr(&t, &x6);
    for (int i = 0; i < 2; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x9, &t, &x3);
    
    ecdsa_field_sqr(&t, &x9);
    ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x11, &t, &x2);
    
    ecdsa_field_sqr(&t, &x11);
    for (int i = 0; i < 10; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x22, &t, &x11);
    
    ecdsa_field_sqr(&t, &x22);
    for (int i = 0; i < 21; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x44, &t, &x22);
    
    ecdsa_field_sqr(&t, &x44);
    for (int i = 0; i < 43; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x88, &t, &x44);
    
    ecdsa_field_sqr(&t, &x88);
    for (int i = 0; i < 87; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x176, &t, &x88);
    
    ecdsa_field_sqr(&t, &x176);
    for (int i = 0; i < 43; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x220, &t, &x44);
    
    ecdsa_field_sqr(&t, &x220);
    for (int i = 0; i < 2; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&x223, &t, &x3);
    
    for (int i = 0; i < 23; i++) ecdsa_field_sqr(&t, &x223);
    ecdsa_field_mul(&t, &t, &x22);
    for (int i = 0; i < 6; i++) ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(&t, &t, &x2);
    ecdsa_field_sqr(&t, &t);
    ecdsa_field_sqr(&t, &t);
    ecdsa_field_mul(r, &t, a);
    
    // Verify: r^2 == a
    ecdsa_field_t check, a_norm;
    ecdsa_field_sqr(&check, r);
    ecdsa_field_normalize(&check);
    a_norm = *a;
    ecdsa_field_normalize(&a_norm);
    
    return ecdsa_field_equal(&check, &a_norm);
}

// =============================================================================
// Batch Inversion (Montgomery's Trick)
// Inverts n field elements using only 1 inversion + 3*(n-1) multiplications
// =============================================================================

void ecdsa_field_inv_batch(ecdsa_field_t *r, const ecdsa_field_t *a, size_t n) {
    if (n == 0) return;
    if (n == 1) {
        ecdsa_field_inv(r, a);
        return;
    }
    
    // Step 1: Compute running products r[i] = a[0] * a[1] * ... * a[i]
    r[0] = a[0];
    for (size_t i = 1; i < n; i++)
        ecdsa_field_mul(&r[i], &r[i-1], &a[i]);
    
    // Step 2: Invert the final product (only 1 field inversion!)
    ecdsa_field_t inv;
    ecdsa_field_inv(&inv, &r[n-1]);
    
    // Step 3: Compute individual inverses by unwinding
    ecdsa_field_t tmp;
    for (size_t i = n - 1; i > 0; i--) {
        ecdsa_field_mul(&tmp, &inv, &r[i-1]);  // r[i] = inv * r[i-1] = 1/a[i]
        ecdsa_field_mul(&inv, &inv, &a[i]);    // inv = inv * a[i]
        r[i] = tmp;
    }
    r[0] = inv;
}
