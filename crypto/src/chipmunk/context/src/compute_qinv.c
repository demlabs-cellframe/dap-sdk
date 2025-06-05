#include <stdio.h>
#include <stdint.h>

#define CHIPMUNK_Q 8380417

// Extended Euclidean Algorithm
int64_t extended_gcd(int64_t a, int64_t b, int64_t *x, int64_t *y) {
    if (a == 0) {
        *x = 0;
        *y = 1;
        return b;
    }
    
    int64_t x1, y1;
    int64_t gcd = extended_gcd(b % a, a, &x1, &y1);
    
    *x = y1 - (b / a) * x1;
    *y = x1;
    
    return gcd;
}

// Find modular inverse
int64_t mod_inverse(int64_t a, int64_t m) {
    int64_t x, y;
    int64_t gcd = extended_gcd(a, m, &x, &y);
    
    if (gcd != 1) {
        printf("Modular inverse does not exist\n");
        return -1;
    }
    
    return (x % m + m) % m;
}

int main() {
    printf("Computing QINV for Chipmunk Montgomery multiplication\n");
    printf("q = %d\n", CHIPMUNK_Q);
    printf("R = 2^32 (for 32-bit Montgomery)\n\n");
    
    // For Montgomery multiplication, we need q^(-1) mod 2^32
    // This is the inverse of q modulo 2^32
    uint64_t R = 1ULL << 32;  // 2^32
    
    // We need to find QINV such that q * QINV ≡ -1 (mod 2^32)
    // This is equivalent to q * QINV ≡ 2^32 - 1 (mod 2^32)
    
    // First find q^(-1) mod 2^32
    int64_t q_inv_mod_R = mod_inverse(CHIPMUNK_Q, R);
    
    // QINV = -q^(-1) mod 2^32 = 2^32 - q^(-1) mod 2^32
    uint32_t QINV = (uint32_t)(R - q_inv_mod_R);
    
    printf("q^(-1) mod 2^32 = %ld\n", q_inv_mod_R);
    printf("QINV = -q^(-1) mod 2^32 = %u\n", QINV);
    printf("QINV (hex) = 0x%X\n", QINV);
    
    // Verify: q * QINV mod 2^32 should equal 2^32 - 1
    uint64_t verify = ((uint64_t)CHIPMUNK_Q * (uint64_t)QINV) & 0xFFFFFFFF;
    printf("\nVerification: q * QINV mod 2^32 = %lu (should be %lu)\n", 
           verify, R - 1);
    
    if (verify == R - 1) {
        printf("✓ QINV is correct!\n");
    } else {
        printf("✗ QINV is incorrect!\n");
    }
    
    // Also compute for the case where we use lower 32 bits
    // In some implementations, QINV is computed as q^(-1) mod 2^32
    uint32_t QINV_alt = (uint32_t)q_inv_mod_R;
    printf("\nAlternative QINV = q^(-1) mod 2^32 = %u\n", QINV_alt);
    printf("Alternative QINV (hex) = 0x%X\n", QINV_alt);
    
    return 0;
} 