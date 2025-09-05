/*
 * Authors:
 * Dmitriy A. Gearasimov <ceo@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dap_math_mod.h"
#include <string.h>

#define LOG_TAG "dap_math_mod"

static bool s_dap_math_mod_initialized = false;

/**
 * @brief Initialize modular arithmetic module
 */
int dap_math_mod_init(void) {
    if (s_dap_math_mod_initialized) {
        return 0;
    }
    s_dap_math_mod_initialized = true;
    log_it(L_INFO, "DAP modular arithmetic module initialized");
    return 0;
}

/**
 * @brief Modular addition: (a + b) mod modulus
 */
int dap_math_mod_add(uint256_t a, uint256_t b, uint256_t modulus, uint256_t *result) {
    if (!result) {
        return -1;
    }

    uint256_t sum;
    int overflow = SUM_256_256(a, b, &sum);

    if (overflow || compare256(sum, modulus) >= 0) {
        SUBTRACT_256_256(sum, modulus, &sum);
    }

    *result = sum;
    return 0;
}

/**
 * @brief Modular subtraction: (a - b) mod modulus
 */
int dap_math_mod_sub(uint256_t a, uint256_t b, uint256_t modulus, uint256_t *result) {
    if (!result) {
        return -1;
    }

    uint256_t diff;
    int underflow = SUBTRACT_256_256(a, b, &diff);

    if (underflow) {
        SUM_256_256(diff, modulus, &diff);
    }

    *result = diff;
    return 0;
}

/**
 * @brief Modular multiplication: (a * b) mod modulus
 */
int dap_math_mod_mul(uint256_t a, uint256_t b, uint256_t modulus, uint256_t *result) {
    if (!result) {
        return -1;
    }

    uint256_t product;
    int overflow = MULT_256_256(a, b, &product);

    if (overflow) {
        // Handle overflow case - this is a simplified approach
        // In production, would need proper big integer handling
        return -2;
    }

    // Reduce modulo modulus
    DIV_256(product, modulus, result);
    return 0;
}

/**
 * @brief Modular exponentiation using binary exponentiation
 */
int dap_math_mod_pow(uint256_t base, uint256_t exponent, uint256_t modulus, uint256_t *result) {
    if (!result) {
        return -1;
    }

    uint256_t res = uint256_1;
    uint256_t b = base;

    // Reduce base modulo modulus first
    DIV_256(b, modulus, &b);

    while (compare256(exponent, uint256_0) > 0) {
        // If exponent is odd, multiply result by base
        uint256_t temp;
        DIV_256(exponent, GET_256_FROM_64(2), &temp);
        LEFT_SHIFT_256(temp, &temp, 1);

        if (compare256(exponent, temp) != 0) {
            // exponent is odd
            uint256_t mul_result;
            if (dap_math_mod_mul(res, b, modulus, &mul_result) != 0) {
                return -1;
            }
            res = mul_result;
        }

        // Square the base
        if (dap_math_mod_mul(b, b, modulus, &b) != 0) {
            return -1;
        }

        // Divide exponent by 2
        DIV_256(exponent, GET_256_FROM_64(2), &exponent);
    }

    *result = res;
    return 0;
}

/**
 * @brief Extended Euclidean algorithm for modular inverse
 */
static int extended_gcd(uint256_t a, uint256_t b, uint256_t *x, uint256_t *y) {
    if (compare256(a, uint256_0) == 0) {
        *x = uint256_0;
        *y = uint256_1;
        return 0;
    }

    uint256_t x1, y1;
    int gcd = extended_gcd(b, a, &x1, &y1);

    uint256_t temp;
    DIV_256(a, b, &temp);
    uint256_t temp2;
    MULT_256_256(temp, y1, &temp2);

    SUBTRACT_256_256(x1, temp2, x);
    *y = y1;

    return gcd;
}

/**
 * @brief Modular inverse using Extended Euclidean algorithm
 */
int dap_math_mod_inverse(uint256_t a, uint256_t modulus, uint256_t *result) {
    if (!result) {
        return -1;
    }

    uint256_t x, y;
    extended_gcd(a, modulus, &x, &y);

    // Make sure x is positive
    if (compare256(x, uint256_0) < 0) {
        SUM_256_256(x, modulus, &x);
    }

    *result = x;
    return 0;
}

/**
 * @brief Check congruence: a ≡ b (mod modulus)
 */
bool dap_math_mod_congruent(uint256_t a, uint256_t b, uint256_t modulus) {
    uint256_t diff;
    SUBTRACT_256_256(a, b, &diff);

    uint256_t remainder;
    DIV_256(diff, modulus, &remainder);

    return compare256(remainder, uint256_0) == 0;
}

/**
 * @brief Precompute Barrett reduction parameter mu
 */
int dap_math_mod_barrett_mu(uint256_t modulus, uint256_t *mu) {
    if (!mu) {
        return -1;
    }

    // For Barrett reduction: mu = 2^(2*k) / modulus
    // where k is the bit length of modulus
    int k = fls256(modulus);

    // Compute 2^(2*k)
    uint256_t two_to_2k = uint256_1;
    LEFT_SHIFT_256(two_to_2k, &two_to_2k, 2 * k);

    // Divide by modulus
    DIV_256(two_to_2k, modulus, mu);

    return 0;
}

/**
 * @brief Barrett reduction
 */
int dap_math_mod_barrett_reduce(uint256_t a, uint256_t modulus, uint256_t mu, uint256_t *result) {
    if (!result) {
        return -1;
    }

    // Barrett reduction: q = floor(a * mu / 2^(2*k))
    // r = a - q * modulus
    // if r >= modulus, r = r - modulus

    uint256_t q;
    MULT_256_256(a, mu, &q);

    int k = fls256(modulus);
    RIGHT_SHIFT_256(q, &q, 2 * k);

    uint256_t q_mod;
    MULT_256_256(q, modulus, &q_mod);

    uint256_t r;
    SUBTRACT_256_256(a, q_mod, &r);

    // Final reduction
    if (compare256(r, modulus) >= 0) {
        SUBTRACT_256_256(r, modulus, &r);
    }

    *result = r;
    return 0;
}

/**
 * @brief Precompute Montgomery parameters
 */
int dap_math_mod_montgomery_params(uint256_t modulus, uint256_t *r2, uint256_t *n_prime) {
    if (!r2 || !n_prime) {
        return -1;
    }

    // For Montgomery reduction, we need:
    // R = 2^k where k is bit length of modulus
    // R^2 = R * R
    // n' = -modulus^(-1) mod R

    int k = fls256(modulus);

    // Compute R = 2^k
    uint256_t R = uint256_1;
    LEFT_SHIFT_256(R, &R, k);

    // Compute R^2
    MULT_256_256(R, R, r2);

    // Compute n' = -modulus^(-1) mod R
    // This is a simplified calculation - in practice would need proper inverse
    uint256_t modulus_inv;
    if (dap_math_mod_inverse(modulus, R, &modulus_inv) != 0) {
        return -1;
    }

    uint256_t neg_modulus_inv;
    SUBTRACT_256_256(uint256_0, modulus_inv, &neg_modulus_inv);
    SUM_256_256(neg_modulus_inv, R, n_prime);

    return 0;
}

/**
 * @brief Montgomery reduction
 */
int dap_math_mod_montgomery_reduce(uint256_t a, uint256_t modulus, uint256_t r2, uint256_t n_prime, uint256_t *result) {
    if (!result) {
        return -1;
    }

    // Montgomery reduction algorithm
    // This is a simplified implementation
    // In production would need proper Montgomery arithmetic

    int k = fls256(modulus);
    uint256_t R = uint256_1;
    LEFT_SHIFT_256(R, &R, k);

    // Compute m = ((a mod R) * n') mod R
    uint256_t a_mod_r;
    DIV_256(a, R, &a_mod_r);

    uint256_t m;
    MULT_256_256(a_mod_r, n_prime, &m);
    DIV_256(m, R, &m);

    // Compute t = (a + m * modulus) / R
    uint256_t m_mod;
    MULT_256_256(m, modulus, &m_mod);

    uint256_t t;
    SUM_256_256(a, m_mod, &t);
    DIV_256(t, R, &t);

    // Final reduction
    if (compare256(t, modulus) >= 0) {
        SUBTRACT_256_256(t, modulus, &t);
    }

    *result = t;
    return 0;
}
