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
    printf("Computing QINV for Chipmunk Montgomery multiplication with R = 2^22\n");
    printf("q = %d\n", CHIPMUNK_Q);
    printf("R = 2^22 = %d\n", 1 << 22);
    
    // For Montgomery multiplication with R = 2^22, we need q^(-1) mod 2^22
    uint32_t R = 1U << 22;  // 2^22
    
    // We need to find QINV such that q * QINV ≡ -1 (mod 2^22)
    // This is equivalent to q * QINV ≡ 2^22 - 1 (mod 2^22)
    
    // First find q^(-1) mod 2^22
    int64_t q_inv_mod_R = mod_inverse(CHIPMUNK_Q, R);
    
    // QINV = -q^(-1) mod 2^22 = 2^22 - q^(-1) mod 2^22
    uint32_t QINV = (uint32_t)(R - q_inv_mod_R);
    
    printf("\nq^(-1) mod 2^22 = %ld\n", q_inv_mod_R);
    printf("QINV = -q^(-1) mod 2^22 = %u\n", QINV);
    
    // Verify: q * QINV mod 2^22 should equal 2^22 - 1
    uint32_t mask = R - 1;
    uint32_t verify = ((uint64_t)CHIPMUNK_Q * (uint64_t)QINV) & mask;
    printf("\nVerification: q * QINV mod 2^22 = %u (should be %u)\n", 
           verify, mask);
    
    if (verify == mask) {
        printf("✓ QINV is correct!\n");
    } else {
        printf("✗ QINV is incorrect!\n");
    }
    
    return 0;
} 