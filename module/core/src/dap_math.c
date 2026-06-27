/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Ltd   https://demlabs.net
 * Copyright  (c) 2025
 * All rights reserved.

This file is part of DAP SDK the open source project

   DAP SDK is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   DAP SDK is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dap_math.h"
#include "dap_common.h"
#include <string.h>

#define LOG_TAG "dap_math"

/**
 * @brief Compute modular inverse using Extended Euclidean Algorithm
 */
int64_t dap_mod_inverse_u64(int64_t a, int64_t b, int64_t mod) {
    // Extended Euclidean Algorithm for modular inverse
    // Returns (a * b^(-1)) mod mod, or 0 if b is not invertible
    
    if (mod <= 0) {
        log_it(L_ERROR, "Invalid modulus: %ld", mod);
        return 0;
    }
    
    if (b == 0) {
        log_it(L_WARNING, "Division by zero in modular inverse");
        return 1;  // Convention: a/0 = a
    }
    
    // Normalize inputs to positive range
    a = ((a % mod) + mod) % mod;
    b = ((b % mod) + mod) % mod;
    
    if (b == 1) {
        return a % mod;  // b^(-1) = 1, so result is just a
    }
    
    // Extended Euclidean Algorithm to find b^(-1) mod mod
    int64_t old_r = mod, r = b;
    int64_t old_s = 0, s = 1;
    
    while (r != 0) {
        int64_t quotient = old_r / r;
        
        // Update r: r_new = old_r - quotient * r
        int64_t temp_r = r;
        r = old_r - quotient * r;
        old_r = temp_r;
        
        // Update s: s_new = old_s - quotient * s
        int64_t temp_s = s;
        s = old_s - quotient * s;
        old_s = temp_s;
    }
    
    if (old_r > 1) {
        // b is not invertible modulo mod (gcd(b, mod) != 1)
        log_it(L_WARNING, "Value %ld is not invertible modulo %ld (gcd = %ld)", b, mod, old_r);
        return 0;
    }
    
    // Ensure positive result
    if (old_s < 0) {
        old_s += mod;
    }
    
    // Compute final result: (a * b^(-1)) mod mod
    int64_t result = (a * old_s) % mod;
    return (result + mod) % mod;
}

/**
 * @brief Compute modular exponentiation using square-and-multiply
 */
uint64_t dap_mod_pow_u64(uint64_t base, uint64_t exponent, uint64_t modulus) {
    if (modulus == 0) {
        log_it(L_ERROR, "Division by zero in modular exponentiation");
        return 0;
    }
    
    if (modulus == 1) {
        return 0;  // Any number mod 1 is 0
    }
    
    uint64_t result = 1;
    base = base % modulus;
    
    while (exponent > 0) {
        // If exponent is odd, multiply base with result
        if (exponent & 1) {
            result = (result * base) % modulus;
        }
        
        // Square the base and halve the exponent
        exponent >>= 1;
        base = (base * base) % modulus;
    }
    
    return result;
}

/**
 * @brief Compute greatest common divisor using Euclidean algorithm
 */
uint64_t dap_gcd_u64(uint64_t a, uint64_t b) {
    while (b != 0) {
        uint64_t temp = b;
        b = a % b;
        a = temp;
    }
    return a;
}

/**
 * @brief Compute least common multiple
 */
uint64_t dap_lcm_u64(uint64_t a, uint64_t b) {
    if (a == 0 || b == 0) {
        return 0;
    }
    
    uint64_t gcd_val = dap_gcd_u64(a, b);
    
    // Check for overflow in multiplication
    if (a / gcd_val > UINT64_MAX / b) {
        log_it(L_ERROR, "Overflow in LCM calculation for %lu and %lu", a, b);
        return 0;
    }
    
    return (a / gcd_val) * b;
}

/**
 * @brief Miller-Rabin primality test
 */
bool dap_is_prime_u64(uint64_t n, int k) {
    if (n < 2) return false;
    if (n == 2 || n == 3) return true;
    if (n % 2 == 0) return false;
    
    // Write n-1 as d * 2^r
    uint64_t d = n - 1;
    int r = 0;
    while (d % 2 == 0) {
        d /= 2;
        r++;
    }
    
    // Perform k rounds of testing
    for (int i = 0; i < k; i++) {
        // Choose random witness a in range [2, n-2]
        uint64_t a = 2 + (rand() % (n - 3));
        
        uint64_t x = dap_mod_pow_u64(a, d, n);
        
        if (x == 1 || x == n - 1) {
            continue;
        }
        
        bool composite = true;
        for (int j = 0; j < r - 1; j++) {
            x = dap_mod_pow_u64(x, 2, n);
            if (x == n - 1) {
                composite = false;
                break;
            }
        }
        
        if (composite) {
            return false;
        }
    }
    
    return true;
}

/**
 * @brief Find next prime number
 */
uint64_t dap_next_prime_u64(uint64_t n) {
    if (n < 2) return 2;
    
    // Make odd if even
    if (n % 2 == 0) n++;
    
    while (!dap_is_prime_u64(n, 10)) {  // 10 rounds for good confidence
        n += 2;  // Only check odd numbers
        
        // Prevent infinite loop on overflow
        if (n < 2) {
            log_it(L_ERROR, "Overflow in prime search");
            return 0;
        }
    }
    
    return n;
}

/**
 * @brief Safe integer addition with overflow detection
 */
bool dap_safe_add_u64(uint64_t a, uint64_t b, uint64_t *result) {
    if (!result) {
        return false;
    }
    
    if (a > UINT64_MAX - b) {
        return false;  // Overflow would occur
    }
    
    *result = a + b;
    return true;
}

/**
 * @brief Safe integer multiplication with overflow detection
 */
bool dap_safe_mul_u64(uint64_t a, uint64_t b, uint64_t *result) {
    if (!result) {
        return false;
    }
    
    if (a == 0 || b == 0) {
        *result = 0;
        return true;
    }
    
    if (a > UINT64_MAX / b) {
        return false;  // Overflow would occur
    }
    
    *result = a * b;
    return true;
}

/**
 * @brief Constant-time memory comparison for cryptographic use
 */
int dap_const_time_memcmp(const void *a, const void *b, size_t size) {
    if (!a || !b) {
        return -1;
    }
    
    const uint8_t *ptr_a = (const uint8_t*)a;
    const uint8_t *ptr_b = (const uint8_t*)b;
    uint8_t result = 0;
    
    // XOR all bytes - result will be 0 only if all bytes match
    for (size_t i = 0; i < size; i++) {
        result |= ptr_a[i] ^ ptr_b[i];
    }
    
    // Return 0 if equal, 1 if different (constant time)
    return result;
}

/**
 * @brief Constant-time conditional move for cryptographic use
 */
void dap_const_time_move(void *dest, const void *src, size_t size, int condition) {
    if (!dest || !src) {
        return;
    }
    
    uint8_t *dest_ptr = (uint8_t*)dest;
    const uint8_t *src_ptr = (const uint8_t*)src;
    
    // Create mask: 0x00 if condition is 0, 0xFF if condition is non-zero
    uint8_t mask = (condition != 0) ? 0xFF : 0x00;
    
    // Conditional copy in constant time
    for (size_t i = 0; i < size; i++) {
        dest_ptr[i] = (dest_ptr[i] & ~mask) | (src_ptr[i] & mask);
    }
}

/**
 * @brief Tonelli-Shanks algorithm for modular square root (advanced implementation)
 */
uint64_t dap_mod_sqrt_u64(uint64_t a, uint64_t p) {
    if (p == 2) {
        return a & 1;
    }
    
    // Check if a is a quadratic residue using Legendre symbol
    if (dap_mod_pow_u64(a, (p - 1) / 2, p) != 1) {
        return 0;  // No square root exists
    }
    
    // Handle simple case: p ≡ 3 (mod 4)
    if ((p & 3) == 3) {
        return dap_mod_pow_u64(a, (p + 1) / 4, p);
    }
    
    // General case: Tonelli-Shanks algorithm
    // Find Q and S such that p - 1 = Q * 2^S with Q odd
    uint64_t Q = p - 1;
    int S = 0;
    while ((Q & 1) == 0) {
        Q >>= 1;
        S++;
    }
    
    // Find a quadratic non-residue z
    uint64_t z = 2;
    while (dap_mod_pow_u64(z, (p - 1) / 2, p) != p - 1) {
        z++;
    }
    
    // Initialize variables
    uint64_t M = S;
    uint64_t c = dap_mod_pow_u64(z, Q, p);
    uint64_t t = dap_mod_pow_u64(a, Q, p);
    uint64_t R = dap_mod_pow_u64(a, (Q + 1) / 2, p);
    
    while (t != 1) {
        // Find the smallest i such that t^(2^i) = 1
        uint64_t temp = t;
        uint64_t i;
        for (i = 1; i < M; i++) {
            temp = (temp * temp) % p;
            if (temp == 1) break;
        }
        
        if (i == M) {
            return 0;  // No solution found
        }
        
        // Update variables
        uint64_t b = dap_mod_pow_u64(c, 1ULL << (M - i - 1), p);
        M = i;
        c = (b * b) % p;
        t = (t * c) % p;
        R = (R * b) % p;
    }
    
    return R;
}
