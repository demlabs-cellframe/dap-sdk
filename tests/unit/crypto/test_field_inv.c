/*
 * Test field inversion specifically
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

static void print_field(const char *label, const ecdsa_field_t *f) {
    uint8_t b[32];
    ecdsa_field_t tmp = *f;
    ecdsa_field_normalize(&tmp);
    ecdsa_field_get_b32(b, &tmp);
    printf("%s: ", label);
    for (int i = 0; i < 32; i++) printf("%02X", b[i]);
    printf("\n");
}

int main(void) {
    printf("=== Testing field inversion ===\n\n");
    
    // Test simple case: inv(5) * 5 should equal 1
    ecdsa_field_t five, inv_five, result, one;
    
    ecdsa_field_set_int(&five, 5);
    ecdsa_field_set_int(&one, 1);
    
    print_limbs("five", &five);
    
    // Step through the addition chain manually
    ecdsa_field_t x2, x3, t1;
    
    // x2 = a^2 * a = a^3
    ecdsa_field_sqr(&x2, &five);
    printf("\nAfter sqr(5):\n");
    print_limbs("x2 = 5^2", &x2);
    print_field("x2 = 5^2", &x2);
    
    ecdsa_field_t expected_25;
    ecdsa_field_set_int(&expected_25, 25);
    print_limbs("expected 25", &expected_25);
    
    ecdsa_field_mul(&x2, &x2, &five);
    printf("\nAfter mul(5^2, 5):\n");
    print_limbs("x2 = 5^3", &x2);
    print_field("x2 = 5^3", &x2);
    
    ecdsa_field_t expected_125;
    ecdsa_field_set_int(&expected_125, 125);
    print_limbs("expected 125", &expected_125);
    
    // x3 = x2^2 * a = a^7
    ecdsa_field_sqr(&x3, &x2);
    printf("\nAfter sqr(5^3):\n");
    print_limbs("x3 = 5^6", &x3);
    print_field("x3 = 5^6", &x3);
    
    // 5^6 = 15625
    ecdsa_field_t expected_15625;
    ecdsa_field_set_int(&expected_15625, 15625);
    print_limbs("expected 15625", &expected_15625);
    
    ecdsa_field_mul(&x3, &x3, &five);
    printf("\nAfter mul(5^6, 5):\n");
    print_limbs("x3 = 5^7", &x3);
    print_field("x3 = 5^7", &x3);
    
    // 5^7 = 78125
    ecdsa_field_t expected_78125;
    ecdsa_field_set_int(&expected_78125, 78125);
    print_limbs("expected 78125", &expected_78125);
    
    // Now test the full inversion
    printf("\n=== Full inversion test ===\n");
    ecdsa_field_inv(&inv_five, &five);
    print_field("inv(5)", &inv_five);
    print_limbs("inv(5)", &inv_five);
    
    ecdsa_field_mul(&result, &five, &inv_five);
    ecdsa_field_normalize(&result);
    print_field("5 * inv(5)", &result);
    print_limbs("5 * inv(5)", &result);
    print_limbs("expected 1", &one);
    
    if (ecdsa_field_equal(&result, &one)) {
        printf("\nPASS: 5 * inv(5) = 1\n");
        return 0;
    } else {
        printf("\nFAIL: 5 * inv(5) != 1\n");
        return 1;
    }
}
