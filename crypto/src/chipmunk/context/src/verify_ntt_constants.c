#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define CHIPMUNK_Q 8380417
#define CHIPMUNK_N 512
#define MONT 4193792    // R = 2^22 mod q

// Modular exponentiation
int64_t mod_pow(int64_t base, int64_t exp, int64_t mod) {
    int64_t result = 1;
    base %= mod;
    while (exp > 0) {
        if (exp & 1) {
            result = (result * base) % mod;
        }
        base = (base * base) % mod;
        exp >>= 1;
    }
    return result;
}

// Montgomery multiplication
int32_t montgomery_multiply(int32_t a, int32_t b) {
    const uint32_t QINV_22 = 4186111; // -q^(-1) mod 2^22
    const uint32_t MASK_22 = (1U << 22) - 1;
    
    int64_t t = (int64_t)a * b;
    uint32_t u = (uint32_t)(t & MASK_22) * QINV_22;
    u &= MASK_22;
    
    t += (int64_t)u * CHIPMUNK_Q;
    int32_t result = (int32_t)(t >> 22);
    
    if (result >= CHIPMUNK_Q) {
        result -= CHIPMUNK_Q;
    }
    
    return result;
}

// Convert to Montgomery form
int32_t to_montgomery(int32_t a) {
    return montgomery_multiply(a, MONT * MONT % CHIPMUNK_Q);
}

int main() {
    printf("Verifying NTT constants for Chipmunk\n");
    printf("Parameters: q = %d, n = %d, R = 2^22 = %d\n\n", CHIPMUNK_Q, CHIPMUNK_N, MONT);
    
    // According to the paper, we need a primitive n-th root of unity
    // For n = 512, we need omega such that omega^512 = 1 (mod q) and omega^256 != 1 (mod q)
    
    // Test some candidates for primitive root
    int64_t candidates[] = {17, 3, 5, 7, 11, 13};
    int64_t omega = -1;
    
    for (int i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        int64_t g = candidates[i];
        // Check if g^((q-1)/n) is a primitive n-th root of unity
        int64_t candidate = mod_pow(g, (CHIPMUNK_Q - 1) / CHIPMUNK_N, CHIPMUNK_Q);
        
        // Check if candidate^n = 1 (mod q)
        if (mod_pow(candidate, CHIPMUNK_N, CHIPMUNK_Q) == 1) {
            // Check if candidate^(n/2) != 1 (mod q) to ensure it's primitive
            if (mod_pow(candidate, CHIPMUNK_N / 2, CHIPMUNK_Q) != 1) {
                omega = candidate;
                printf("Found primitive %d-th root of unity: %ld (generator g = %ld)\n", 
                       CHIPMUNK_N, omega, g);
                break;
            }
        }
    }
    
    if (omega == -1) {
        printf("Failed to find primitive root of unity!\n");
        return 1;
    }
    
    // According to the documentation, omega = 1753 for q = 8380417, n = 512
    // Let's verify this
    int64_t omega_doc = 1753;
    printf("\nVerifying omega = %ld from documentation:\n", omega_doc);
    printf("omega^%d mod q = %ld (should be 1)\n", 
           CHIPMUNK_N, mod_pow(omega_doc, CHIPMUNK_N, CHIPMUNK_Q));
    printf("omega^%d mod q = %ld (should NOT be 1)\n", 
           CHIPMUNK_N/2, mod_pow(omega_doc, CHIPMUNK_N/2, CHIPMUNK_Q));
    
    // If documentation value is correct, use it
    if (mod_pow(omega_doc, CHIPMUNK_N, CHIPMUNK_Q) == 1 && 
        mod_pow(omega_doc, CHIPMUNK_N/2, CHIPMUNK_Q) != 1) {
        omega = omega_doc;
        printf("Using omega = %ld from documentation\n", omega);
    }
    
    // Convert omega to Montgomery form
    int32_t omega_mont = to_montgomery(omega);
    printf("\nOmega in Montgomery form: %d\n", omega_mont);
    
    // Generate first few zetas
    printf("\nFirst few zetas in Montgomery form:\n");
    int64_t zeta = omega;
    for (int i = 1; i <= 10; i++) {
        int32_t zeta_mont = to_montgomery(zeta);
        printf("zetas[%d] = %d (normal form: %ld)\n", i, zeta_mont, zeta);
        zeta = (zeta * omega) % CHIPMUNK_Q;
    }
    
    return 0;
} 