#include <stdio.h>
#include <stdint.h>

#define CHIPMUNK_Q 8380417
#define CHIPMUNK_N 512
#define MONT 4193792    // R = 2^22 mod q

// Extended Euclidean Algorithm to find modular inverse
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

// Find modular inverse of a modulo m
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
    printf("Computing n_inv_mont for Chipmunk with n=%d, q=%d\n", CHIPMUNK_N, CHIPMUNK_Q);
    printf("Montgomery constant R = 2^22 = %d\n", MONT);
    
    // First compute n^(-1) mod q
    int64_t n_inv = mod_inverse(CHIPMUNK_N, CHIPMUNK_Q);
    printf("n^(-1) mod q = %ld\n", n_inv);
    
    // Then compute n_inv_mont = (n^(-1) * R) mod q
    int64_t n_inv_mont = (n_inv * MONT) % CHIPMUNK_Q;
    printf("n_inv_mont = (n^(-1) * R) mod q = %ld\n", n_inv_mont);
    
    // Verify: (n * n_inv_mont * R^(-1)) mod q should equal 1
    int64_t r_inv = mod_inverse(MONT, CHIPMUNK_Q);
    int64_t verification = (((int64_t)CHIPMUNK_N * n_inv_mont) % CHIPMUNK_Q * r_inv) % CHIPMUNK_Q;
    printf("Verification: (n * n_inv_mont * R^(-1)) mod q = %ld (should be 1)\n", verification);
    
    return 0;
} 