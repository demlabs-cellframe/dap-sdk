#include <stdio.h>
#include <stdint.h>
#include <string.h>

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
    // To convert to Montgomery form, multiply by R^2 mod q
    int64_t R2 = ((int64_t)MONT * MONT) % CHIPMUNK_Q;
    return montgomery_multiply(a, R2);
}

int main() {
    printf("Generating correct NTT constants for Chipmunk\n");
    printf("Parameters: q = %d, n = %d, omega = 1753\n\n", CHIPMUNK_Q, CHIPMUNK_N);
    
    int64_t omega = 1753;  // Primitive 512-th root of unity
    
    // For standard Cooley-Tukey NTT, we need powers of omega
    // The zetas array should contain omega^(brv(k)) for k = 0, 1, ..., n-1
    // where brv is bit-reversal
    
    // But for the iterative NTT as implemented in Chipmunk, we need
    // the twiddle factors in a specific order
    
    printf("const int32_t g_zetas_mont[CHIPMUNK_ZETAS_MONT_LEN] = {\n");
    printf("    0, // placeholder for index 0\n");
    
    int idx = 1;
    // Generate zetas for forward NTT
    // For each stage of the NTT
    for (int len = CHIPMUNK_N / 2; len >= 1; len /= 2) {
        // Root for this stage: omega^(n/(2*len))
        int64_t root = mod_pow(omega, CHIPMUNK_N / (2 * len), CHIPMUNK_Q);
        int64_t zeta = 1;
        
        for (int j = 0; j < len; j++) {
            int32_t zeta_mont = to_montgomery(zeta);
            
            if (idx % 8 == 0) {
                printf("%8d,\n", zeta_mont);
            } else if (idx % 8 == 1) {
                printf("    %d,", zeta_mont);
            } else {
                printf("%8d,", zeta_mont);
            }
            
            zeta = (zeta * root) % CHIPMUNK_Q;
            idx++;
        }
    }
    
    printf("\n};\n\n");
    
    printf("Total zetas generated: %d\n", idx - 1);
    printf("CHIPMUNK_ZETAS_MONT_LEN should be: %d\n", idx);
    
    // Verify omega
    printf("\nVerification:\n");
    printf("omega = %ld\n", omega);
    printf("omega^512 mod q = %ld (should be 1)\n", mod_pow(omega, CHIPMUNK_N, CHIPMUNK_Q));
    printf("omega^256 mod q = %ld (should be %d)\n", mod_pow(omega, CHIPMUNK_N/2, CHIPMUNK_Q), CHIPMUNK_Q - 1);
    
    // Test first few values
    printf("\nFirst few zetas (normal form):\n");
    int64_t test_omega = omega;
    for (int i = 1; i <= 10; i++) {
        printf("omega^%d = %ld\n", i, test_omega);
        test_omega = (test_omega * omega) % CHIPMUNK_Q;
    }
    
    return 0;
} 