/*
 * Detailed debug tests for native ECDSA implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// Include internal headers directly for testing
#include "ecdsa_field.h"
#include "ecdsa_scalar.h"
#include "ecdsa_group.h"

static void print_field(const char *label, const ecdsa_field_t *f) {
    uint8_t b[32];
    ecdsa_field_t tmp = *f;
    ecdsa_field_normalize(&tmp);
    ecdsa_field_get_b32(b, &tmp);
    printf("%s: ", label);
    for (int i = 0; i < 32; i++) printf("%02X", b[i]);
    printf("\n");
}

static void print_point(const char *label, const ecdsa_ge_t *p) {
    printf("%s:\n", label);
    if (p->infinity) {
        printf("  INFINITY\n");
    } else {
        print_field("  X", &p->x);
        print_field("  Y", &p->y);
    }
}

static void print_jacobian(const char *label, const ecdsa_gej_t *p) {
    printf("%s (Jacobian):\n", label);
    if (p->infinity) {
        printf("  INFINITY\n");
    } else {
        print_field("  X", &p->x);
        print_field("  Y", &p->y);
        print_field("  Z", &p->z);
    }
}

// Test field operations
static bool test_field_basic(void) {
    printf("\n=== Test: Field Basic Operations ===\n");
    
    ecdsa_field_t a, b, c;
    
    // Test: 2 + 3 = 5
    ecdsa_field_set_int(&a, 2);
    ecdsa_field_set_int(&b, 3);
    ecdsa_field_add(&c, &a, &b);
    ecdsa_field_normalize(&c);
    
    ecdsa_field_t expected;
    ecdsa_field_set_int(&expected, 5);
    
    if (!ecdsa_field_equal(&c, &expected)) {
        printf("FAIL: 2 + 3 != 5\n");
        print_field("Got", &c);
        return false;
    }
    printf("PASS: 2 + 3 = 5\n");
    
    // Test: 2 * 3 = 6
    ecdsa_field_mul(&c, &a, &b);
    ecdsa_field_set_int(&expected, 6);
    if (!ecdsa_field_equal(&c, &expected)) {
        printf("FAIL: 2 * 3 != 6\n");
        print_field("Got", &c);
        return false;
    }
    printf("PASS: 2 * 3 = 6\n");
    
    // Test: 5^2 = 25
    ecdsa_field_set_int(&a, 5);
    ecdsa_field_sqr(&c, &a);
    ecdsa_field_set_int(&expected, 25);
    if (!ecdsa_field_equal(&c, &expected)) {
        printf("FAIL: 5^2 != 25\n");
        print_field("Got", &c);
        return false;
    }
    printf("PASS: 5^2 = 25\n");
    
    // Test: 5 * inv(5) = 1
    ecdsa_field_set_int(&a, 5);
    ecdsa_field_inv(&b, &a);
    ecdsa_field_mul(&c, &a, &b);
    ecdsa_field_normalize(&c);
    ecdsa_field_set_int(&expected, 1);
    if (!ecdsa_field_equal(&c, &expected)) {
        printf("FAIL: 5 * inv(5) != 1\n");
        print_field("Got", &c);
        return false;
    }
    printf("PASS: 5 * inv(5) = 1\n");
    
    return true;
}

// Test point operations
static bool test_point_double(void) {
    printf("\n=== Test: Point Doubling ===\n");
    
    // Initialize generator
    ecdsa_ecmult_gen_init();
    
    // Get generator as affine point
    const uint8_t gx[32] = {
        0x79, 0xBE, 0x66, 0x7E, 0xF9, 0xDC, 0xBB, 0xAC,
        0x55, 0xA0, 0x62, 0x95, 0xCE, 0x87, 0x0B, 0x07,
        0x02, 0x9B, 0xFC, 0xDB, 0x2D, 0xCE, 0x28, 0xD9,
        0x59, 0xF2, 0x81, 0x5B, 0x16, 0xF8, 0x17, 0x98
    };
    const uint8_t gy[32] = {
        0x48, 0x3A, 0xDA, 0x77, 0x26, 0xA3, 0xC4, 0x65,
        0x5D, 0xA4, 0xFB, 0xFC, 0x0E, 0x11, 0x08, 0xA8,
        0xFD, 0x17, 0xB4, 0x48, 0xA6, 0x85, 0x54, 0x19,
        0x9C, 0x47, 0xD0, 0x8F, 0xFB, 0x10, 0xD4, 0xB8
    };
    
    // Expected 2*G
    const uint8_t expected_2g_x[32] = {
        0xC6, 0x04, 0x7F, 0x94, 0x41, 0xED, 0x7D, 0x6D,
        0x30, 0x45, 0x40, 0x6E, 0x95, 0xC0, 0x7C, 0xD8,
        0x5C, 0x77, 0x8E, 0x4B, 0x8C, 0xEF, 0x3C, 0xA7,
        0xAB, 0xAC, 0x09, 0xB9, 0x5C, 0x70, 0x9E, 0xE5
    };
    const uint8_t expected_2g_y[32] = {
        0x1A, 0xE1, 0x68, 0xFE, 0xA6, 0x3D, 0xC3, 0x39,
        0xA3, 0xC5, 0x84, 0x19, 0x46, 0x6C, 0xEA, 0xEE,
        0xF7, 0xF6, 0x32, 0x65, 0x32, 0x66, 0xD0, 0xE1,
        0x23, 0x64, 0x31, 0xA9, 0x50, 0xCF, 0xE5, 0x2A
    };
    
    ecdsa_ge_t G;
    ecdsa_field_set_b32(&G.x, gx);
    ecdsa_field_set_b32(&G.y, gy);
    G.infinity = false;
    
    print_point("G", &G);
    
    // Check G is on curve
    if (!ecdsa_ge_is_valid(&G)) {
        printf("FAIL: G is not on curve!\n");
        return false;
    }
    printf("PASS: G is on curve\n");
    
    // Convert to Jacobian
    ecdsa_gej_t Gj;
    ecdsa_gej_set_ge(&Gj, &G);
    print_jacobian("G (Jacobian)", &Gj);
    
    // Double
    ecdsa_gej_t result;
    ecdsa_gej_double(&result, &Gj);
    print_jacobian("2*G (Jacobian)", &result);
    
    // Convert back to affine
    ecdsa_ge_t result_affine;
    ecdsa_ge_set_gej(&result_affine, &result);
    print_point("2*G (Affine)", &result_affine);
    
    // Check result
    uint8_t got_x[32], got_y[32];
    ecdsa_field_get_b32(got_x, &result_affine.x);
    ecdsa_field_get_b32(got_y, &result_affine.y);
    
    printf("\nExpected 2*G.x: ");
    for (int i = 0; i < 32; i++) printf("%02X", expected_2g_x[i]);
    printf("\nGot      2*G.x: ");
    for (int i = 0; i < 32; i++) printf("%02X", got_x[i]);
    printf("\n");
    
    printf("Expected 2*G.y: ");
    for (int i = 0; i < 32; i++) printf("%02X", expected_2g_y[i]);
    printf("\nGot      2*G.y: ");
    for (int i = 0; i < 32; i++) printf("%02X", got_y[i]);
    printf("\n");
    
    if (memcmp(got_x, expected_2g_x, 32) != 0 || memcmp(got_y, expected_2g_y, 32) != 0) {
        printf("FAIL: 2*G mismatch\n");
        return false;
    }
    
    printf("PASS: 2*G computed correctly\n");
    return true;
}

// Test scalar multiplication
static bool test_ecmult_gen(void) {
    printf("\n=== Test: Scalar Multiplication ===\n");
    
    ecdsa_ecmult_gen_init();
    
    // Test n=1 -> G
    ecdsa_scalar_t one;
    ecdsa_scalar_set_int(&one, 1);
    
    ecdsa_gej_t result;
    ecdsa_ecmult_gen(&result, &one);
    
    ecdsa_ge_t result_affine;
    ecdsa_ge_set_gej(&result_affine, &result);
    
    print_point("1*G", &result_affine);
    
    // Test n=2 -> 2*G
    ecdsa_scalar_t two;
    ecdsa_scalar_set_int(&two, 2);
    
    ecdsa_ecmult_gen(&result, &two);
    ecdsa_ge_set_gej(&result_affine, &result);
    
    print_point("2*G via ecmult_gen", &result_affine);
    
    // Compare with direct doubling
    const uint8_t gx[32] = {
        0x79, 0xBE, 0x66, 0x7E, 0xF9, 0xDC, 0xBB, 0xAC,
        0x55, 0xA0, 0x62, 0x95, 0xCE, 0x87, 0x0B, 0x07,
        0x02, 0x9B, 0xFC, 0xDB, 0x2D, 0xCE, 0x28, 0xD9,
        0x59, 0xF2, 0x81, 0x5B, 0x16, 0xF8, 0x17, 0x98
    };
    const uint8_t gy[32] = {
        0x48, 0x3A, 0xDA, 0x77, 0x26, 0xA3, 0xC4, 0x65,
        0x5D, 0xA4, 0xFB, 0xFC, 0x0E, 0x11, 0x08, 0xA8,
        0xFD, 0x17, 0xB4, 0x48, 0xA6, 0x85, 0x54, 0x19,
        0x9C, 0x47, 0xD0, 0x8F, 0xFB, 0x10, 0xD4, 0xB8
    };
    
    ecdsa_ge_t G;
    ecdsa_field_set_b32(&G.x, gx);
    ecdsa_field_set_b32(&G.y, gy);
    G.infinity = false;
    
    ecdsa_gej_t Gj;
    ecdsa_gej_set_ge(&Gj, &G);
    
    ecdsa_gej_t doubled;
    ecdsa_gej_double(&doubled, &Gj);
    
    ecdsa_ge_t doubled_affine;
    ecdsa_ge_set_gej(&doubled_affine, &doubled);
    
    print_point("2*G via direct double", &doubled_affine);
    
    // Compare
    uint8_t got1_x[32], got1_y[32], got2_x[32], got2_y[32];
    ecdsa_field_get_b32(got1_x, &result_affine.x);
    ecdsa_field_get_b32(got1_y, &result_affine.y);
    ecdsa_field_get_b32(got2_x, &doubled_affine.x);
    ecdsa_field_get_b32(got2_y, &doubled_affine.y);
    
    if (memcmp(got1_x, got2_x, 32) != 0 || memcmp(got1_y, got2_y, 32) != 0) {
        printf("FAIL: ecmult_gen(2) != direct double\n");
        return false;
    }
    
    printf("PASS: ecmult_gen(2) == direct double\n");
    return true;
}

int main(void) {
    printf("====================================\n");
    printf("ECDSA Debug Tests\n");
    printf("====================================\n");
    
    int passed = 0, failed = 0;
    
    if (test_field_basic()) passed++; else failed++;
    if (test_point_double()) passed++; else failed++;
    if (test_ecmult_gen()) passed++; else failed++;
    
    printf("\n====================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    printf("====================================\n");
    
    return failed > 0 ? 1 : 0;
}
