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

// Bit-reversal permutation
unsigned int bitrev(unsigned int v, int bits) {
    unsigned int r = 0;
    for (int i = 0; i < bits; i++) {
        r = (r << 1) | (v & 1);
        v >>= 1;
    }
    return r;
}

int main() {
    printf("Generating correct NTT constants for Chipmunk\n");
    printf("Parameters: q = %d, n = %d, omega = 1753\n\n", CHIPMUNK_Q, CHIPMUNK_N);
    
    int64_t omega = 1753;  // Primitive 512-th root of unity
    
    // Generate zetas array for NTT
    // According to Chipmunk implementation, we need n zetas
    int32_t zetas[CHIPMUNK_N];
    
    // Generate powers of omega in bit-reversed order
    for (int i = 0; i < CHIPMUNK_N; i++) {
        unsigned int idx = bitrev(i, 9); // log2(512) = 9
        int64_t zeta = mod_pow(omega, idx, CHIPMUNK_Q);
        zetas[i] = to_montgomery(zeta);
    }
    
    // Print the array in C format
    printf("const int32_t g_zetas_mont[CHIPMUNK_ZETAS_MONT_LEN] = {\n");
    printf("    0, // placeholder for index 0\n");
    
    // For Chipmunk NTT, we need zetas in specific order for Cooley-Tukey NTT
    // Generate zetas for forward NTT
    int k = 0;
    for (int len = CHIPMUNK_N / 2; len > 0; len >>= 1) {
        for (int start = 0; start < CHIPMUNK_N; start += 2 * len) {
            int64_t zeta = mod_pow(omega, (CHIPMUNK_N / (2 * len)) * k, CHIPMUNK_Q);
            int32_t zeta_mont = to_montgomery(zeta);
            
            if (k < CHIPMUNK_N - 1) {
                if ((k + 1) % 8 == 0) {
                    printf("    %d,\n", zeta_mont);
                } else {
                    printf("    %d,", zeta_mont);
                }
            }
            k++;
            if (k >= CHIPMUNK_N) break;
        }
        if (k >= CHIPMUNK_N) break;
    }
    
    printf("\n};\n\n");
    
    // Verify some values
    printf("Verification:\n");
    printf("omega = %ld\n", omega);
    printf("omega^512 mod q = %ld (should be 1)\n", mod_pow(omega, CHIPMUNK_N, CHIPMUNK_Q));
    printf("omega^256 mod q = %ld (should be %d)\n", mod_pow(omega, CHIPMUNK_N/2, CHIPMUNK_Q), CHIPMUNK_Q - 1);
    
    return 0;
} 