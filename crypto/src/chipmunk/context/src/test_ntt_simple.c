#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define CHIPMUNK_N 512
#define CHIPMUNK_Q 8380417

// Простой тест NTT
void test_ntt_simple() {
    int32_t test_poly[CHIPMUNK_N];
    int32_t backup[CHIPMUNK_N];
    
    // Создаём простой тестовый полином
    memset(test_poly, 0, sizeof(test_poly));
    test_poly[0] = 1;  // Полином x^0 = 1
    test_poly[1] = 2;  // + 2*x^1
    test_poly[2] = 3;  // + 3*x^2
    
    // Сохраняем копию
    memcpy(backup, test_poly, sizeof(test_poly));
    
    printf("Original polynomial: ");
    for (int i = 0; i < 8; i++) {
        printf("%d ", test_poly[i]);
    }
    printf("...\n");
    
    // Применяем прямое NTT
    extern void chipmunk_ntt(int32_t a_r[CHIPMUNK_N]);
    chipmunk_ntt(test_poly);
    
    printf("After NTT: ");
    for (int i = 0; i < 8; i++) {
        printf("%d ", test_poly[i]);
    }
    printf("...\n");
    
    // Применяем обратное NTT
    extern void chipmunk_invntt(int32_t a_r[CHIPMUNK_N]);
    chipmunk_invntt(test_poly);
    
    printf("After inverse NTT: ");
    for (int i = 0; i < 8; i++) {
        printf("%d ", test_poly[i]);
    }
    printf("...\n");
    
    // Проверяем, восстановился ли исходный полином
    int errors = 0;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        if (test_poly[i] != backup[i]) {
            errors++;
        }
    }
    
    if (errors == 0) {
        printf("✓ NTT test PASSED: polynomial correctly restored\n");
    } else {
        printf("✗ NTT test FAILED: %d coefficients differ\n", errors);
    }
}

int main() {
    printf("Testing NTT with n=%d, q=%d\n", CHIPMUNK_N, CHIPMUNK_Q);
    test_ntt_simple();
    return 0;
} 