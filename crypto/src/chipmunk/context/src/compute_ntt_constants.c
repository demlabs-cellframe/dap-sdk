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

// Modular exponentiation
int64_t mod_pow(int64_t base, int64_t exp, int64_t mod) {
    int64_t result = 1;
    base = base % mod;
    while (exp > 0) {
        if (exp % 2 == 1) {
            result = (result * base) % mod;
        }
        exp = exp >> 1;
        base = (base * base) % mod;
    }
    return result;
}

// Find primitive root of unity
int64_t find_primitive_root(int64_t q, int64_t n) {
    // For q = 8380417, we need to find a primitive 512-th root of unity
    // q - 1 = 8380416 = 2^10 * 8184 = 1024 * 8184
    // So we need omega such that omega^512 = 1 (mod q) and omega^256 != 1 (mod q)
    
    int64_t order = (q - 1) / n;  // 8380416 / 512 = 16368
    
    // Try different generators
    for (int64_t g = 2; g < q; g++) {
        int64_t omega = mod_pow(g, order, q);
        
        // Check if omega is a primitive n-th root of unity
        if (mod_pow(omega, n, q) == 1 && mod_pow(omega, n/2, q) != 1) {
            return omega;
        }
    }
    
    return -1;
}

// Convert to Montgomery form
int64_t to_montgomery(int64_t a, int64_t q, int64_t mont) {
    return (a * mont) % q;
}

int main() {
    printf("Computing NTT constants for Chipmunk:\n");
    printf("q = %d, n = %d, MONT = %d\n\n", CHIPMUNK_Q, CHIPMUNK_N, MONT);
    
    // Find primitive root of unity
    int64_t omega = find_primitive_root(CHIPMUNK_Q, CHIPMUNK_N);
    if (omega == -1) {
        printf("Failed to find primitive root of unity\n");
        return 1;
    }
    
    printf("Primitive %d-th root of unity: %ld\n", CHIPMUNK_N, omega);
    
    // Verify omega
    printf("Verification: omega^%d mod q = %ld (should be 1)\n", 
           CHIPMUNK_N, mod_pow(omega, CHIPMUNK_N, CHIPMUNK_Q));
    printf("Verification: omega^%d mod q = %ld (should not be 1)\n", 
           CHIPMUNK_N/2, mod_pow(omega, CHIPMUNK_N/2, CHIPMUNK_Q));
    
    // Generate zetas array for NTT
    printf("\nGenerating zetas array in Montgomery form:\n");
    printf("const int32_t g_zetas_mont[%d] = {\n", CHIPMUNK_N);
    
    // First element is 0 (placeholder)
    printf("    0, // placeholder\n");
    
    // Generate forward NTT constants
    int64_t omega_power = 1;
    for (int i = 1; i < CHIPMUNK_N; i++) {
        if (i > 1) {
            omega_power = (omega_power * omega) % CHIPMUNK_Q;
        } else {
            omega_power = omega;
        }
        
        int64_t zeta_mont = to_montgomery(omega_power, CHIPMUNK_Q, MONT);
        printf("    %ld", zeta_mont);
        if (i < CHIPMUNK_N - 1) printf(",");
        if (i % 8 == 0) printf("\n");
    }
    
    printf("\n};\n\n");
    
    // Compute n^(-1) in Montgomery form
    int64_t n_inv = mod_inverse(CHIPMUNK_N, CHIPMUNK_Q);
    int64_t n_inv_mont = to_montgomery(n_inv, CHIPMUNK_Q, MONT);
    
    printf("n^(-1) mod q = %ld\n", n_inv);
    printf("n^(-1) in Montgomery form = %ld\n", n_inv_mont);
    
    return 0;
} 