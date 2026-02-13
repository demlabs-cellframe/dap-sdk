/*
 * Test field multiplication with larger numbers
 */
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "ecdsa_field.h"

static void print_limbs(const char *label, const ecdsa_field_t *f) {
    printf("%s limbs: [%016llX, %016llX, %016llX, %016llX, %016llX]\n", 
           label, 
           (unsigned long long)f->n[0], 
           (unsigned long long)f->n[1], 
           (unsigned long long)f->n[2], 
           (unsigned long long)f->n[3], 
           (unsigned long long)f->n[4]);
}

static void print_hex(const char *label, const ecdsa_field_t *f) {
    uint8_t b[32];
    ecdsa_field_t tmp = *f;
    ecdsa_field_normalize(&tmp);
    ecdsa_field_get_b32(b, &tmp);
    printf("%s: ", label);
    for (int i = 0; i < 32; i++) printf("%02X", b[i]);
    printf("\n");
}

int main(void) {
    printf("=== Testing field multiplication ===\n\n");
    
    // Test 1: Small numbers
    printf("Test 1: Small numbers\n");
    ecdsa_field_t a, b, c;
    ecdsa_field_set_int(&a, 12345);
    ecdsa_field_set_int(&b, 67890);
    ecdsa_field_mul(&c, &a, &b);
    ecdsa_field_normalize(&c);
    
    // 12345 * 67890 = 838102050
    ecdsa_field_t expected;
    expected.n[0] = 838102050ULL;
    expected.n[1] = expected.n[2] = expected.n[3] = expected.n[4] = 0;
    
    print_limbs("a", &a);
    print_limbs("b", &b);
    print_limbs("a*b", &c);
    print_limbs("expected", &expected);
    
    if (ecdsa_field_equal(&c, &expected)) {
        printf("PASS: 12345 * 67890 = 838102050\n\n");
    } else {
        printf("FAIL: 12345 * 67890 != 838102050\n\n");
    }
    
#ifdef ECDSA_FIELD_52BIT
    // Test 2: Larger numbers (still fits in one limb) - 52-bit limbs only
    printf("Test 2: Larger numbers in single limb\n");
    ecdsa_field_set_int(&a, 0);
    a.n[0] = 0xFFFFFFFFFFFFULL;  // 48 bits
    ecdsa_field_set_int(&b, 0);
    b.n[0] = 2;
    ecdsa_field_mul(&c, &a, &b);
    ecdsa_field_normalize(&c);
    
    // 0xFFFFFFFFFFFF * 2 = 0x1FFFFFFFFFFFE
    expected.n[0] = 0x1FFFFFFFFFFEULL & 0xFFFFFFFFFFFFFULL;  // 52 bits
    expected.n[1] = 0x1FFFFFFFFFFEULL >> 52;
    expected.n[2] = expected.n[3] = expected.n[4] = 0;
    
    print_limbs("a", &a);
    print_limbs("b", &b);
    print_limbs("a*b", &c);
    print_limbs("expected", &expected);
    
    if (ecdsa_field_equal(&c, &expected)) {
        printf("PASS\n\n");
    } else {
        printf("FAIL\n\n");
    }
    
    // Test 3: Numbers spanning multiple limbs - 52-bit limbs only
    printf("Test 3: Multi-limb multiplication\n");
    
    // a = 2^100
    ecdsa_field_set_int(&a, 0);
    a.n[1] = 1ULL << 48;  // 2^100 = 2^(52+48)
    
    // b = 2^100
    ecdsa_field_set_int(&b, 0);
    b.n[1] = 1ULL << 48;
    
    ecdsa_field_mul(&c, &a, &b);
    ecdsa_field_normalize(&c);
    
    // 2^100 * 2^100 = 2^200
    // 2^200 = 2^(52*3 + 44) = limb[3] bit 44
    expected.n[0] = expected.n[1] = expected.n[2] = 0;
    expected.n[3] = 1ULL << 44;  // 52*3=156, so 200-156=44
    expected.n[4] = 0;
    
    print_limbs("a = 2^100", &a);
    print_limbs("b = 2^100", &b);
    print_limbs("a*b = 2^200", &c);
    print_limbs("expected", &expected);
    
    if (ecdsa_field_equal(&c, &expected)) {
        printf("PASS\n\n");
    } else {
        printf("FAIL\n\n");
    }
#else
    printf("Test 2 & 3: Skipped (26-bit limbs - tests use 52-bit values)\n\n");
#endif
    
    // Test 4: Known value from secp256k1 - Gx * Gy
    printf("Test 4: Gx * Gy (known curve point coordinates)\n");
    
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
    
    ecdsa_field_set_b32(&a, gx);
    ecdsa_field_set_b32(&b, gy);
    
    print_hex("Gx", &a);
    print_hex("Gy", &b);
    print_limbs("Gx", &a);
    print_limbs("Gy", &b);
    
    ecdsa_field_mul(&c, &a, &b);
    ecdsa_field_normalize(&c);
    
    print_hex("Gx * Gy", &c);
    print_limbs("Gx * Gy", &c);
    
    // Test a * a^(-1) = 1 step by step
    printf("\nTest 5: Squaring a few times\n");
    ecdsa_field_t five, sq;
    ecdsa_field_set_int(&five, 5);
    
    // 5^2 = 25
    ecdsa_field_sqr(&sq, &five);
    print_hex("5^2", &sq);
    
    // 25^2 = 625
    ecdsa_field_sqr(&sq, &sq);
    print_hex("5^4", &sq);
    
    // 625^2 = 390625
    ecdsa_field_sqr(&sq, &sq);
    print_hex("5^8", &sq);
    
    // Continue
    ecdsa_field_sqr(&sq, &sq);
    print_hex("5^16", &sq);
    
    ecdsa_field_sqr(&sq, &sq);
    print_hex("5^32", &sq);
    
    ecdsa_field_sqr(&sq, &sq);
    print_hex("5^64", &sq);
    
    // 5^64 should still be small
    // 5^64 = 5.42101086242752e44 = about 2^148
    printf("Expected 5^64 ~ 2^148 which spans 3 limbs\n");
    
    return 0;
}
