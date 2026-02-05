/*
 * Internal ECDSA scalar arithmetic implementation (mod n)
 * 
 * secp256k1 order: n = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
 * 
 * Uses 4x64-bit representation on 64-bit platforms,
 * or 8x32-bit representation on 32-bit platforms.
 */

#include "ecdsa_scalar.h"
#include <string.h>

// =============================================================================
// Architecture-specific implementations
// =============================================================================

#ifdef ECDSA_SCALAR_64BIT

// -----------------------------------------------------------------------------
// 64-bit: 4x64-bit limb representation
// -----------------------------------------------------------------------------

const ecdsa_scalar_t ECDSA_SCALAR_N = {{
    0xBFD25E8CD0364141ULL, 0xBAAEDCE6AF48A03BULL,
    0xFFFFFFFFFFFFFFFEULL, 0xFFFFFFFFFFFFFFFFULL
}};

const ecdsa_scalar_t ECDSA_SCALAR_N_HALF = {{
    0xDFE92F46681B20A0ULL, 0x5D576E7357A4501DULL,
    0xFFFFFFFFFFFFFFFFULL, 0x7FFFFFFFFFFFFFFFULL
}};

const ecdsa_scalar_t ECDSA_SCALAR_ZERO = {{0, 0, 0, 0}};
const ecdsa_scalar_t ECDSA_SCALAR_ONE = {{1, 0, 0, 0}};

void ecdsa_scalar_set_b32(ecdsa_scalar_t *r, const uint8_t *b32, int *overflow) {
    r->d[3] = ((uint64_t)b32[0] << 56) | ((uint64_t)b32[1] << 48) |
              ((uint64_t)b32[2] << 40) | ((uint64_t)b32[3] << 32) |
              ((uint64_t)b32[4] << 24) | ((uint64_t)b32[5] << 16) |
              ((uint64_t)b32[6] << 8)  | (uint64_t)b32[7];
    r->d[2] = ((uint64_t)b32[8] << 56) | ((uint64_t)b32[9] << 48) |
              ((uint64_t)b32[10] << 40) | ((uint64_t)b32[11] << 32) |
              ((uint64_t)b32[12] << 24) | ((uint64_t)b32[13] << 16) |
              ((uint64_t)b32[14] << 8)  | (uint64_t)b32[15];
    r->d[1] = ((uint64_t)b32[16] << 56) | ((uint64_t)b32[17] << 48) |
              ((uint64_t)b32[18] << 40) | ((uint64_t)b32[19] << 32) |
              ((uint64_t)b32[20] << 24) | ((uint64_t)b32[21] << 16) |
              ((uint64_t)b32[22] << 8)  | (uint64_t)b32[23];
    r->d[0] = ((uint64_t)b32[24] << 56) | ((uint64_t)b32[25] << 48) |
              ((uint64_t)b32[26] << 40) | ((uint64_t)b32[27] << 32) |
              ((uint64_t)b32[28] << 24) | ((uint64_t)b32[29] << 16) |
              ((uint64_t)b32[30] << 8)  | (uint64_t)b32[31];
    
    int over = 0;
    if (r->d[3] > ECDSA_SCALAR_N.d[3]) over = 1;
    else if (r->d[3] == ECDSA_SCALAR_N.d[3]) {
        if (r->d[2] > ECDSA_SCALAR_N.d[2]) over = 1;
        else if (r->d[2] == ECDSA_SCALAR_N.d[2]) {
            if (r->d[1] > ECDSA_SCALAR_N.d[1]) over = 1;
            else if (r->d[1] == ECDSA_SCALAR_N.d[1]) {
                if (r->d[0] >= ECDSA_SCALAR_N.d[0]) over = 1;
            }
        }
    }
    
    if (over) {
        uint64_t borrow = 0, t;
        t = r->d[0] - ECDSA_SCALAR_N.d[0]; borrow = t > r->d[0]; r->d[0] = t;
        t = r->d[1] - ECDSA_SCALAR_N.d[1] - borrow; borrow = (t > r->d[1]) || (borrow && t == r->d[1]); r->d[1] = t;
        t = r->d[2] - ECDSA_SCALAR_N.d[2] - borrow; borrow = (t > r->d[2]) || (borrow && t == r->d[2]); r->d[2] = t;
        r->d[3] = r->d[3] - ECDSA_SCALAR_N.d[3] - borrow;
    }
    
    if (overflow) *overflow = over;
}

void ecdsa_scalar_get_b32(uint8_t *b32, const ecdsa_scalar_t *a) {
    for (int i = 0; i < 8; i++) b32[i] = (a->d[3] >> (56 - i*8)) & 0xFF;
    for (int i = 0; i < 8; i++) b32[8+i] = (a->d[2] >> (56 - i*8)) & 0xFF;
    for (int i = 0; i < 8; i++) b32[16+i] = (a->d[1] >> (56 - i*8)) & 0xFF;
    for (int i = 0; i < 8; i++) b32[24+i] = (a->d[0] >> (56 - i*8)) & 0xFF;
}

bool ecdsa_scalar_is_high(const ecdsa_scalar_t *a) {
    for (int i = ECDSA_SCALAR_LIMBS - 1; i >= 0; i--) {
        if (a->d[i] > ECDSA_SCALAR_N_HALF.d[i]) return true;
        if (a->d[i] < ECDSA_SCALAR_N_HALF.d[i]) return false;
    }
    return false;
}

void ecdsa_scalar_negate(ecdsa_scalar_t *r, const ecdsa_scalar_t *a) {
    if (ecdsa_scalar_is_zero(a)) { ecdsa_scalar_clear(r); return; }
    
    uint64_t borrow = 0, t;
    t = ECDSA_SCALAR_N.d[0] - a->d[0]; borrow = t > ECDSA_SCALAR_N.d[0];
    r->d[0] = t;
    t = ECDSA_SCALAR_N.d[1] - a->d[1] - borrow; borrow = (t > ECDSA_SCALAR_N.d[1]) || (borrow && t == ECDSA_SCALAR_N.d[1]);
    r->d[1] = t;
    t = ECDSA_SCALAR_N.d[2] - a->d[2] - borrow; borrow = (t > ECDSA_SCALAR_N.d[2]) || (borrow && t == ECDSA_SCALAR_N.d[2]);
    r->d[2] = t;
    r->d[3] = ECDSA_SCALAR_N.d[3] - a->d[3] - borrow;
}

// Helper: check if 4-limb value >= n
static inline int scalar_ge_n(const uint64_t *v) {
    for (int i = 3; i >= 0; i--) {
        if (v[i] > ECDSA_SCALAR_N.d[i]) return 1;
        if (v[i] < ECDSA_SCALAR_N.d[i]) return 0;
    }
    return 1;
}

// Helper: subtract n from 4-limb value
static inline void scalar_sub_n(uint64_t *v) {
    uint64_t borrow = 0, t;
    for (int i = 0; i < 4; i++) {
        t = v[i] - ECDSA_SCALAR_N.d[i] - borrow;
        borrow = (t > v[i]) || (borrow && v[i] == 0);
        v[i] = t;
    }
}

int ecdsa_scalar_add(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b) {
    uint64_t carry = 0, t;
    for (int i = 0; i < 4; i++) {
        t = a->d[i] + b->d[i] + carry;
        carry = (t < a->d[i]) || (carry && t == a->d[i]);
        r->d[i] = t;
    }
    
    int reduce = carry || scalar_ge_n(r->d);
    if (reduce) scalar_sub_n(r->d);
    return reduce;
}

// 2^256 mod n constant
static const uint64_t SECP256K1_N_C[3] = {
    0x402DA1732FC9BEBFULL, 0x4551231950B75FC4ULL, 0x1ULL
};

#ifdef __SIZEOF_INT128__
typedef __uint128_t uint128_t;
#define MUL128(a, b) ((__uint128_t)(a) * (b))
#define LO64(x) ((uint64_t)(x))
#define HI64(x) ((uint64_t)((x) >> 64))
#endif

void ecdsa_scalar_mul(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b) {
#ifdef __SIZEOF_INT128__
    uint64_t l[8] = {0};
    uint128_t c;
    
    for (int i = 0; i < 4; i++) {
        c = 0;
        for (int j = 0; j < 4; j++) {
            c += (uint128_t)l[i+j] + MUL128(a->d[i], b->d[j]);
            l[i+j] = LO64(c);
            c = HI64(c);
        }
        l[i+4] += LO64(c);
    }
    
    while (l[4] | l[5] | l[6] | l[7]) {
        uint64_t upper[4] = {l[4], l[5], l[6], l[7]};
        uint64_t product[8] = {0};
        
        for (int i = 0; i < 4; i++) {
            c = 0;
            for (int j = 0; j < 3; j++) {
                c += (uint128_t)product[i+j] + MUL128(upper[i], SECP256K1_N_C[j]);
                product[i+j] = LO64(c);
                c = HI64(c);
            }
            product[i+3] += LO64(c);
        }
        
        c = 0;
        for (int i = 0; i < 4; i++) { c += (uint128_t)l[i] + product[i]; l[i] = LO64(c); c = HI64(c); }
        for (int i = 4; i < 8; i++) { c += product[i]; l[i] = LO64(c); c = HI64(c); }
    }
    
    while (scalar_ge_n(l)) scalar_sub_n(l);
    for (int i = 0; i < 4; i++) r->d[i] = l[i];
#else
    // Fallback using 32-bit multiplies
    uint64_t l[8] = {0};
    for (int i = 0; i < 4; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < 4; j++) {
            __uint128_t p = (__uint128_t)a->d[i] * b->d[j] + l[i+j] + carry;
            l[i+j] = (uint64_t)p;
            carry = (uint64_t)(p >> 64);
        }
        l[i+4] += carry;
    }
    while (l[4] | l[5] | l[6] | l[7]) {
        uint64_t upper[4] = {l[4], l[5], l[6], l[7]};
        uint64_t product[8] = {0};
        for (int i = 0; i < 4; i++) {
            uint64_t carry = 0;
            for (int j = 0; j < 3; j++) {
                __uint128_t p = (__uint128_t)upper[i] * SECP256K1_N_C[j] + product[i+j] + carry;
                product[i+j] = (uint64_t)p;
                carry = (uint64_t)(p >> 64);
            }
            product[i+3] += carry;
        }
        uint64_t carry = 0;
        for (int i = 0; i < 4; i++) {
            __uint128_t s = (__uint128_t)l[i] + product[i] + carry;
            l[i] = (uint64_t)s;
            carry = (uint64_t)(s >> 64);
        }
        for (int i = 4; i < 8; i++) l[i] = product[i];
    }
    while (scalar_ge_n(l)) scalar_sub_n(l);
    for (int i = 0; i < 4; i++) r->d[i] = l[i];
#endif
}

#else // ECDSA_SCALAR_32BIT

// -----------------------------------------------------------------------------
// 32-bit: 8x32-bit limb representation (little-endian)
// -----------------------------------------------------------------------------

const ecdsa_scalar_t ECDSA_SCALAR_N = {{
    0xD0364141UL, 0xBFD25E8CUL, 0xAF48A03BUL, 0xBAAEDCE6UL,
    0xFFFFFFFEUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL
}};

const ecdsa_scalar_t ECDSA_SCALAR_N_HALF = {{
    0x681B20A0UL, 0xDFE92F46UL, 0x57A4501DUL, 0x5D576E73UL,
    0xFFFFFFFFUL, 0xFFFFFFFFUL, 0xFFFFFFFFUL, 0x7FFFFFFFUL
}};

const ecdsa_scalar_t ECDSA_SCALAR_ZERO = {{0, 0, 0, 0, 0, 0, 0, 0}};
const ecdsa_scalar_t ECDSA_SCALAR_ONE = {{1, 0, 0, 0, 0, 0, 0, 0}};

void ecdsa_scalar_set_b32(ecdsa_scalar_t *r, const uint8_t *b32, int *overflow) {
    // Big-endian to little-endian 32-bit limbs
    for (int i = 0; i < 8; i++) {
        int idx = 28 - i * 4;
        r->d[i] = ((uint32_t)b32[idx] << 24) | ((uint32_t)b32[idx+1] << 16) |
                  ((uint32_t)b32[idx+2] << 8) | (uint32_t)b32[idx+3];
    }
    
    // Check overflow
    int over = 0;
    for (int i = 7; i >= 0; i--) {
        if (r->d[i] > ECDSA_SCALAR_N.d[i]) { over = 1; break; }
        if (r->d[i] < ECDSA_SCALAR_N.d[i]) break;
    }
    
    if (over) {
        uint32_t borrow = 0;
        for (int i = 0; i < 8; i++) {
            uint64_t t = (uint64_t)r->d[i] - ECDSA_SCALAR_N.d[i] - borrow;
            r->d[i] = (uint32_t)t;
            borrow = (t >> 32) ? 1 : 0;
        }
    }
    
    if (overflow) *overflow = over;
}

void ecdsa_scalar_get_b32(uint8_t *b32, const ecdsa_scalar_t *a) {
    for (int i = 0; i < 8; i++) {
        int idx = 28 - i * 4;
        b32[idx] = (a->d[i] >> 24) & 0xFF;
        b32[idx+1] = (a->d[i] >> 16) & 0xFF;
        b32[idx+2] = (a->d[i] >> 8) & 0xFF;
        b32[idx+3] = a->d[i] & 0xFF;
    }
}

bool ecdsa_scalar_is_high(const ecdsa_scalar_t *a) {
    for (int i = 7; i >= 0; i--) {
        if (a->d[i] > ECDSA_SCALAR_N_HALF.d[i]) return true;
        if (a->d[i] < ECDSA_SCALAR_N_HALF.d[i]) return false;
    }
    return false;
}

void ecdsa_scalar_negate(ecdsa_scalar_t *r, const ecdsa_scalar_t *a) {
    if (ecdsa_scalar_is_zero(a)) { ecdsa_scalar_clear(r); return; }
    
    uint32_t borrow = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t t = (uint64_t)ECDSA_SCALAR_N.d[i] - a->d[i] - borrow;
        r->d[i] = (uint32_t)t;
        borrow = (t >> 32) ? 1 : 0;
    }
}

// Helper: check if 8-limb value >= n
static inline int scalar_ge_n(const uint32_t *v) {
    for (int i = 7; i >= 0; i--) {
        if (v[i] > ECDSA_SCALAR_N.d[i]) return 1;
        if (v[i] < ECDSA_SCALAR_N.d[i]) return 0;
    }
    return 1;
}

// Helper: subtract n from 8-limb value
static inline void scalar_sub_n(uint32_t *v) {
    uint32_t borrow = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t t = (uint64_t)v[i] - ECDSA_SCALAR_N.d[i] - borrow;
        v[i] = (uint32_t)t;
        borrow = (t >> 32) ? 1 : 0;
    }
}

int ecdsa_scalar_add(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b) {
    uint32_t carry = 0;
    for (int i = 0; i < 8; i++) {
        uint64_t t = (uint64_t)a->d[i] + b->d[i] + carry;
        r->d[i] = (uint32_t)t;
        carry = (uint32_t)(t >> 32);
    }
    
    int reduce = carry || scalar_ge_n(r->d);
    if (reduce) scalar_sub_n(r->d);
    return reduce;
}

// 2^256 mod n in 32-bit limbs (little-endian)
static const uint32_t SECP256K1_N_C_32[5] = {
    0x2FC9BEBFUL, 0x402DA173UL, 0x50B75FC4UL, 0x45512319UL, 0x1UL
};

void ecdsa_scalar_mul(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b) {
    uint32_t l[16] = {0};
    
    // Schoolbook multiplication
    for (int i = 0; i < 8; i++) {
        uint32_t carry = 0;
        for (int j = 0; j < 8; j++) {
            uint64_t p = (uint64_t)a->d[i] * b->d[j] + l[i+j] + carry;
            l[i+j] = (uint32_t)p;
            carry = (uint32_t)(p >> 32);
        }
        l[i+8] += carry;
    }
    
    // Reduce upper 256 bits using 2^256 mod n
    while (l[8] | l[9] | l[10] | l[11] | l[12] | l[13] | l[14] | l[15]) {
        uint32_t upper[8] = {l[8], l[9], l[10], l[11], l[12], l[13], l[14], l[15]};
        uint32_t product[16] = {0};
        
        for (int i = 0; i < 8; i++) {
            uint32_t carry = 0;
            for (int j = 0; j < 5; j++) {
                uint64_t p = (uint64_t)upper[i] * SECP256K1_N_C_32[j] + product[i+j] + carry;
                product[i+j] = (uint32_t)p;
                carry = (uint32_t)(p >> 32);
            }
            product[i+5] += carry;
        }
        
        uint32_t carry = 0;
        for (int i = 0; i < 8; i++) {
            uint64_t t = (uint64_t)l[i] + product[i] + carry;
            l[i] = (uint32_t)t;
            carry = (uint32_t)(t >> 32);
        }
        for (int i = 8; i < 16; i++) {
            uint64_t t = (uint64_t)product[i] + carry;
            l[i] = (uint32_t)t;
            carry = (uint32_t)(t >> 32);
        }
    }
    
    while (scalar_ge_n(l)) scalar_sub_n(l);
    for (int i = 0; i < 8; i++) r->d[i] = l[i];
}

#endif // ECDSA_SCALAR_64BIT

// =============================================================================
// Common functions (architecture-independent)
// =============================================================================

void ecdsa_scalar_clear(ecdsa_scalar_t *r) {
    memset(r->d, 0, sizeof(r->d));
}

void ecdsa_scalar_copy(ecdsa_scalar_t *r, const ecdsa_scalar_t *a) {
    *r = *a;
}

void ecdsa_scalar_set_int(ecdsa_scalar_t *r, unsigned int v) {
    ecdsa_scalar_clear(r);
    r->d[0] = v;
}

bool ecdsa_scalar_is_zero(const ecdsa_scalar_t *a) {
    ecdsa_scalar_limb_t z = 0;
    for (int i = 0; i < ECDSA_SCALAR_LIMBS; i++)
        z |= a->d[i];
    return z == 0;
}

bool ecdsa_scalar_equal(const ecdsa_scalar_t *a, const ecdsa_scalar_t *b) {
    ecdsa_scalar_limb_t diff = 0;
    for (int i = 0; i < ECDSA_SCALAR_LIMBS; i++)
        diff |= a->d[i] ^ b->d[i];
    return diff == 0;
}

// Modular inverse using Fermat's little theorem: a^(-1) = a^(n-2) mod n
void ecdsa_scalar_inv(ecdsa_scalar_t *r, const ecdsa_scalar_t *a) {
    // n-2 in big-endian bytes
    static const uint8_t n_minus_2[32] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
        0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
        0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x3F
    };
    
    ecdsa_scalar_t x = *a;
    ecdsa_scalar_t result;
    ecdsa_scalar_set_int(&result, 1);
    
    for (int i = 0; i < 32; i++) {
        for (int j = 7; j >= 0; j--) {
            ecdsa_scalar_mul(&result, &result, &result);
            if ((n_minus_2[i] >> j) & 1) {
                ecdsa_scalar_mul(&result, &result, &x);
            }
        }
    }
    
    *r = result;
}

bool ecdsa_scalar_check_seckey(const uint8_t *seckey) {
    ecdsa_scalar_t s;
    int overflow = 0;
    
    ecdsa_scalar_set_b32(&s, seckey, &overflow);
    
    if (overflow) return false;
    if (ecdsa_scalar_is_zero(&s)) return false;
    
    return true;
}
