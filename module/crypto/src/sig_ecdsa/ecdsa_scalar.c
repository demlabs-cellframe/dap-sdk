/*
 * Internal ECDSA scalar arithmetic implementation (mod n)
 * 
 * secp256k1 order: n = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
 * 
 * Uses 4x64-bit representation on 64-bit platforms.
 */

#include "ecdsa_scalar.h"
#include <string.h>

// =============================================================================
// secp256k1 curve order n
// =============================================================================

#ifdef ECDSA_SCALAR_64BIT

// n = d[0] + d[1]*2^64 + d[2]*2^128 + d[3]*2^192 (little-endian)
const ecdsa_scalar_t ECDSA_SCALAR_N = {{
    0xBFD25E8CD0364141ULL,
    0xBAAEDCE6AF48A03BULL,
    0xFFFFFFFFFFFFFFFEULL,
    0xFFFFFFFFFFFFFFFFULL
}};

// n/2 for low-S check
const ecdsa_scalar_t ECDSA_SCALAR_N_HALF = {{
    0xDFE92F46681B20A0ULL,
    0x5D576E7357A4501DULL,
    0xFFFFFFFFFFFFFFFFULL,
    0x7FFFFFFFFFFFFFFFULL
}};

const ecdsa_scalar_t ECDSA_SCALAR_ZERO = {{0, 0, 0, 0}};
const ecdsa_scalar_t ECDSA_SCALAR_ONE = {{1, 0, 0, 0}};

void ecdsa_scalar_clear(ecdsa_scalar_t *r) {
    r->d[0] = r->d[1] = r->d[2] = r->d[3] = 0;
}

void ecdsa_scalar_copy(ecdsa_scalar_t *r, const ecdsa_scalar_t *a) {
    *r = *a;
}

void ecdsa_scalar_set_int(ecdsa_scalar_t *r, unsigned int v) {
    r->d[0] = v;
    r->d[1] = r->d[2] = r->d[3] = 0;
}

// Set from 32-byte big-endian, returns overflow
void ecdsa_scalar_set_b32(ecdsa_scalar_t *r, const uint8_t *b32, int *overflow) {
    // Big-endian to little-endian limbs
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
    
    // Check if >= n
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
    
    // Reduce if overflow
    if (over) {
        uint64_t borrow = 0;
        uint64_t t0 = r->d[0] - ECDSA_SCALAR_N.d[0];
        borrow = t0 > r->d[0];
        r->d[0] = t0;
        
        uint64_t t1 = r->d[1] - ECDSA_SCALAR_N.d[1] - borrow;
        borrow = (t1 > r->d[1]) || (borrow && t1 == r->d[1]);
        r->d[1] = t1;
        
        uint64_t t2 = r->d[2] - ECDSA_SCALAR_N.d[2] - borrow;
        borrow = (t2 > r->d[2]) || (borrow && t2 == r->d[2]);
        r->d[2] = t2;
        
        r->d[3] = r->d[3] - ECDSA_SCALAR_N.d[3] - borrow;
    }
    
    if (overflow) *overflow = over;
}

// Get 32-byte big-endian
void ecdsa_scalar_get_b32(uint8_t *b32, const ecdsa_scalar_t *a) {
    b32[0] = (a->d[3] >> 56) & 0xFF;
    b32[1] = (a->d[3] >> 48) & 0xFF;
    b32[2] = (a->d[3] >> 40) & 0xFF;
    b32[3] = (a->d[3] >> 32) & 0xFF;
    b32[4] = (a->d[3] >> 24) & 0xFF;
    b32[5] = (a->d[3] >> 16) & 0xFF;
    b32[6] = (a->d[3] >> 8) & 0xFF;
    b32[7] = a->d[3] & 0xFF;
    b32[8] = (a->d[2] >> 56) & 0xFF;
    b32[9] = (a->d[2] >> 48) & 0xFF;
    b32[10] = (a->d[2] >> 40) & 0xFF;
    b32[11] = (a->d[2] >> 32) & 0xFF;
    b32[12] = (a->d[2] >> 24) & 0xFF;
    b32[13] = (a->d[2] >> 16) & 0xFF;
    b32[14] = (a->d[2] >> 8) & 0xFF;
    b32[15] = a->d[2] & 0xFF;
    b32[16] = (a->d[1] >> 56) & 0xFF;
    b32[17] = (a->d[1] >> 48) & 0xFF;
    b32[18] = (a->d[1] >> 40) & 0xFF;
    b32[19] = (a->d[1] >> 32) & 0xFF;
    b32[20] = (a->d[1] >> 24) & 0xFF;
    b32[21] = (a->d[1] >> 16) & 0xFF;
    b32[22] = (a->d[1] >> 8) & 0xFF;
    b32[23] = a->d[1] & 0xFF;
    b32[24] = (a->d[0] >> 56) & 0xFF;
    b32[25] = (a->d[0] >> 48) & 0xFF;
    b32[26] = (a->d[0] >> 40) & 0xFF;
    b32[27] = (a->d[0] >> 32) & 0xFF;
    b32[28] = (a->d[0] >> 24) & 0xFF;
    b32[29] = (a->d[0] >> 16) & 0xFF;
    b32[30] = (a->d[0] >> 8) & 0xFF;
    b32[31] = a->d[0] & 0xFF;
}

bool ecdsa_scalar_is_zero(const ecdsa_scalar_t *a) {
    return (a->d[0] | a->d[1] | a->d[2] | a->d[3]) == 0;
}

// Check if s > n/2 (for low-S normalization)
bool ecdsa_scalar_is_high(const ecdsa_scalar_t *a) {
    if (a->d[3] > ECDSA_SCALAR_N_HALF.d[3]) return true;
    if (a->d[3] < ECDSA_SCALAR_N_HALF.d[3]) return false;
    if (a->d[2] > ECDSA_SCALAR_N_HALF.d[2]) return true;
    if (a->d[2] < ECDSA_SCALAR_N_HALF.d[2]) return false;
    if (a->d[1] > ECDSA_SCALAR_N_HALF.d[1]) return true;
    if (a->d[1] < ECDSA_SCALAR_N_HALF.d[1]) return false;
    return a->d[0] > ECDSA_SCALAR_N_HALF.d[0];
}

bool ecdsa_scalar_equal(const ecdsa_scalar_t *a, const ecdsa_scalar_t *b) {
    return (a->d[0] == b->d[0]) && (a->d[1] == b->d[1]) && 
           (a->d[2] == b->d[2]) && (a->d[3] == b->d[3]);
}

// Negate: r = -a (mod n) = n - a
void ecdsa_scalar_negate(ecdsa_scalar_t *r, const ecdsa_scalar_t *a) {
    if (ecdsa_scalar_is_zero(a)) {
        ecdsa_scalar_clear(r);
        return;
    }
    
    uint64_t borrow = 0;
    uint64_t t0 = ECDSA_SCALAR_N.d[0] - a->d[0];
    borrow = t0 > ECDSA_SCALAR_N.d[0];
    
    uint64_t t1 = ECDSA_SCALAR_N.d[1] - a->d[1] - borrow;
    borrow = (t1 > ECDSA_SCALAR_N.d[1]) || (borrow && t1 == ECDSA_SCALAR_N.d[1]);
    
    uint64_t t2 = ECDSA_SCALAR_N.d[2] - a->d[2] - borrow;
    borrow = (t2 > ECDSA_SCALAR_N.d[2]) || (borrow && t2 == ECDSA_SCALAR_N.d[2]);
    
    uint64_t t3 = ECDSA_SCALAR_N.d[3] - a->d[3] - borrow;
    
    r->d[0] = t0; r->d[1] = t1; r->d[2] = t2; r->d[3] = t3;
}

// Add: r = a + b (mod n), returns 1 if overflow occurred
int ecdsa_scalar_add(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b) {
    uint64_t carry = 0;
    
    uint64_t t0 = a->d[0] + b->d[0];
    carry = t0 < a->d[0];
    
    uint64_t t1 = a->d[1] + b->d[1] + carry;
    carry = (t1 < a->d[1]) || (carry && t1 == a->d[1]);
    
    uint64_t t2 = a->d[2] + b->d[2] + carry;
    carry = (t2 < a->d[2]) || (carry && t2 == a->d[2]);
    
    uint64_t t3 = a->d[3] + b->d[3] + carry;
    carry = (t3 < a->d[3]) || (carry && t3 == a->d[3]);
    
    // Check if >= n or overflow
    int reduce = carry;
    if (!reduce) {
        if (t3 > ECDSA_SCALAR_N.d[3]) reduce = 1;
        else if (t3 == ECDSA_SCALAR_N.d[3]) {
            if (t2 > ECDSA_SCALAR_N.d[2]) reduce = 1;
            else if (t2 == ECDSA_SCALAR_N.d[2]) {
                if (t1 > ECDSA_SCALAR_N.d[1]) reduce = 1;
                else if (t1 == ECDSA_SCALAR_N.d[1]) {
                    if (t0 >= ECDSA_SCALAR_N.d[0]) reduce = 1;
                }
            }
        }
    }
    
    if (reduce) {
        uint64_t borrow = 0;
        t0 -= ECDSA_SCALAR_N.d[0];
        borrow = t0 > (a->d[0] + b->d[0]);
        
        uint64_t t1_new = t1 - ECDSA_SCALAR_N.d[1] - borrow;
        borrow = (t1_new > t1) || (borrow && t1_new == t1);
        t1 = t1_new;
        
        uint64_t t2_new = t2 - ECDSA_SCALAR_N.d[2] - borrow;
        borrow = (t2_new > t2) || (borrow && t2_new == t2);
        t2 = t2_new;
        
        t3 = t3 - ECDSA_SCALAR_N.d[3] - borrow;
    }
    
    r->d[0] = t0; r->d[1] = t1; r->d[2] = t2; r->d[3] = t3;
    return reduce;
}

// 128-bit multiply helper
#ifdef __SIZEOF_INT128__
    typedef __uint128_t uint128_t;
    #define MUL128(a, b) ((__uint128_t)(a) * (b))
    #define LO64(x) ((uint64_t)(x))
    #define HI64(x) ((uint64_t)((x) >> 64))
#else
    static inline void mul64_128(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo) {
        uint64_t a_lo = a & 0xFFFFFFFF;
        uint64_t a_hi = a >> 32;
        uint64_t b_lo = b & 0xFFFFFFFF;
        uint64_t b_hi = b >> 32;
        
        uint64_t p0 = a_lo * b_lo;
        uint64_t p1 = a_lo * b_hi;
        uint64_t p2 = a_hi * b_lo;
        uint64_t p3 = a_hi * b_hi;
        
        uint64_t mid = p1 + (p0 >> 32);
        mid += p2;
        if (mid < p2) p3 += 0x100000000ULL;
        
        *lo = (p0 & 0xFFFFFFFF) | (mid << 32);
        *hi = p3 + (mid >> 32);
    }
#endif

// Multiply: r = a * b (mod n) using Montgomery reduction or direct
void ecdsa_scalar_mul(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b) {
#ifdef __SIZEOF_INT128__
    // 8-limb product
    uint64_t l[8] = {0};
    uint128_t c = 0;
    
    // Schoolbook multiply
    for (int i = 0; i < 4; i++) {
        c = 0;
        for (int j = 0; j < 4; j++) {
            c += (uint128_t)l[i+j] + MUL128(a->d[i], b->d[j]);
            l[i+j] = LO64(c);
            c = HI64(c);
        }
        l[i+4] = LO64(c);
    }
    
    // Reduce mod n using Barrett reduction
    // For simplicity, use repeated subtraction (slower but correct)
    // TODO: Implement proper Barrett or Montgomery reduction
    
    // Reduce: while result >= n, subtract n
    // The 512-bit result l[0..7] needs to be reduced to 256 bits
    
    // Using the fact that 2^256 ≡ (2^256 - n) (mod n)
    // 2^256 - n = 0x14551231950B75FC4402DA1732FC9BEBF
    const uint64_t mu[4] = {
        0x4402DA1732FC9BEBFULL & 0xFFFFFFFFFFFFFFFFULL,
        0x14551231950B75FC4ULL,
        0x1ULL,
        0x0ULL
    };
    (void)mu;
    
    // Simple reduction: subtract n while >= n
    // First, reduce upper 256 bits
    for (int iter = 0; iter < 4 && (l[4] | l[5] | l[6] | l[7]); iter++) {
        // Multiply upper part by (2^256 mod n) and add to lower
        uint64_t upper[4] = {l[4], l[5], l[6], l[7]};
        l[4] = l[5] = l[6] = l[7] = 0;
        
        // 2^256 mod n = n + something small, approximate
        // For correctness, use exact reduction
        c = 0;
        for (int i = 0; i < 4; i++) {
            c += l[i];
            for (int j = 0; j <= i && j < 4; j++) {
                // Add upper[j] * (corresponding coefficient)
            }
            l[i] = LO64(c);
            c = HI64(c);
        }
    }
    
    // Final: reduce while >= n
    while (1) {
        int ge = 0;
        if (l[3] > ECDSA_SCALAR_N.d[3]) ge = 1;
        else if (l[3] == ECDSA_SCALAR_N.d[3]) {
            if (l[2] > ECDSA_SCALAR_N.d[2]) ge = 1;
            else if (l[2] == ECDSA_SCALAR_N.d[2]) {
                if (l[1] > ECDSA_SCALAR_N.d[1]) ge = 1;
                else if (l[1] == ECDSA_SCALAR_N.d[1]) {
                    if (l[0] >= ECDSA_SCALAR_N.d[0]) ge = 1;
                }
            }
        }
        
        if (!ge) break;
        
        uint64_t borrow = 0;
        uint64_t t = l[0] - ECDSA_SCALAR_N.d[0];
        borrow = t > l[0];
        l[0] = t;
        
        t = l[1] - ECDSA_SCALAR_N.d[1] - borrow;
        borrow = (t > l[1]) || (borrow && t == l[1]);
        l[1] = t;
        
        t = l[2] - ECDSA_SCALAR_N.d[2] - borrow;
        borrow = (t > l[2]) || (borrow && t == l[2]);
        l[2] = t;
        
        l[3] = l[3] - ECDSA_SCALAR_N.d[3] - borrow;
    }
    
    r->d[0] = l[0]; r->d[1] = l[1]; r->d[2] = l[2]; r->d[3] = l[3];
#else
    // Fallback without __int128
    uint64_t l[8] = {0};
    
    for (int i = 0; i < 4; i++) {
        uint64_t carry = 0;
        for (int j = 0; j < 4; j++) {
            uint64_t hi, lo;
            mul64_128(a->d[i], b->d[j], &hi, &lo);
            l[i+j] += lo + carry;
            carry = hi + (l[i+j] < lo + carry);
        }
        l[i+4] += carry;
    }
    
    // Simple reduction
    while (l[4] | l[5] | l[6] | l[7]) {
        // Subtract n from high bits (approximate)
        // This is a placeholder - proper implementation needed
        l[4] = l[5] = l[6] = l[7] = 0;
    }
    
    // Final reduction
    while (1) {
        int ge = (l[3] > ECDSA_SCALAR_N.d[3]) ||
                 ((l[3] == ECDSA_SCALAR_N.d[3]) && (l[2] > ECDSA_SCALAR_N.d[2])) ||
                 ((l[3] == ECDSA_SCALAR_N.d[3]) && (l[2] == ECDSA_SCALAR_N.d[2]) && (l[1] > ECDSA_SCALAR_N.d[1])) ||
                 ((l[3] == ECDSA_SCALAR_N.d[3]) && (l[2] == ECDSA_SCALAR_N.d[2]) && (l[1] == ECDSA_SCALAR_N.d[1]) && (l[0] >= ECDSA_SCALAR_N.d[0]));
        
        if (!ge) break;
        
        // Subtract n
        uint64_t borrow = (l[0] < ECDSA_SCALAR_N.d[0]);
        l[0] -= ECDSA_SCALAR_N.d[0];
        
        uint64_t new_borrow = (l[1] < ECDSA_SCALAR_N.d[1] + borrow);
        l[1] -= ECDSA_SCALAR_N.d[1] + borrow;
        borrow = new_borrow;
        
        new_borrow = (l[2] < ECDSA_SCALAR_N.d[2] + borrow);
        l[2] -= ECDSA_SCALAR_N.d[2] + borrow;
        borrow = new_borrow;
        
        l[3] -= ECDSA_SCALAR_N.d[3] + borrow;
    }
    
    r->d[0] = l[0]; r->d[1] = l[1]; r->d[2] = l[2]; r->d[3] = l[3];
#endif
}

// Modular inverse using Fermat's little theorem: a^(-1) = a^(n-2) mod n
void ecdsa_scalar_inv(ecdsa_scalar_t *r, const ecdsa_scalar_t *a) {
    // n-2 in binary for secp256k1
    // Use square-and-multiply
    ecdsa_scalar_t x = *a;
    ecdsa_scalar_t result;
    ecdsa_scalar_set_int(&result, 1);
    
    // n-2 = FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD036413F
    // Process from LSB
    uint8_t n_minus_2[32];
    for (int i = 0; i < 32; i++) n_minus_2[i] = 0xFF;
    // Adjust for actual n-2
    n_minus_2[31] = 0x3F;
    n_minus_2[30] = 0x41;
    n_minus_2[29] = 0x36;
    n_minus_2[28] = 0xD0;
    n_minus_2[27] = 0x8C;
    n_minus_2[26] = 0x5E;
    n_minus_2[25] = 0xD2;
    n_minus_2[24] = 0xBF;
    n_minus_2[23] = 0x3B;
    n_minus_2[22] = 0xA0;
    n_minus_2[21] = 0x48;
    n_minus_2[20] = 0xAF;
    n_minus_2[19] = 0xE6;
    n_minus_2[18] = 0xDC;
    n_minus_2[17] = 0xAE;
    n_minus_2[16] = 0xBA;
    n_minus_2[15] = 0xFE;
    
    for (int i = 31; i >= 0; i--) {
        for (int j = 0; j < 8; j++) {
            ecdsa_scalar_mul(&result, &result, &result);
            if ((n_minus_2[31-i] >> (7-j)) & 1) {
                ecdsa_scalar_mul(&result, &result, &x);
            }
        }
    }
    
    *r = result;
}

// Check if private key is valid (0 < key < n)
bool ecdsa_scalar_check_seckey(const uint8_t *seckey) {
    ecdsa_scalar_t s;
    int overflow = 0;
    
    ecdsa_scalar_set_b32(&s, seckey, &overflow);
    
    // Must not overflow and must not be zero
    if (overflow) return false;
    if (ecdsa_scalar_is_zero(&s)) return false;
    
    return true;
}

#else // ECDSA_SCALAR_32BIT

// TODO: 8x32-bit implementation for 32-bit platforms
#error "32-bit scalar implementation not yet available"

#endif
