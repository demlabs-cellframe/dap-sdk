#include <stdio.h>
#include <stdint.h>

#define CHIPMUNK_Q 8380417
#define QINV 4236238847

// Функция для вычисления модульного обратного
uint64_t mod_inverse(uint64_t a, uint64_t m) {
    int64_t m0 = m, x0 = 0, x1 = 1;
    if (m == 1) return 0;
    while (a > 1) {
        int64_t q = a / m;
        int64_t t = m;
        m = a % m;
        a = t;
        t = x0;
        x0 = x1 - q * x0;
        x1 = t;
    }
    if (x1 < 0) x1 += m0;
    return x1;
}

int main() {
    printf("Проверка константы QINV для Chipmunk\n");
    printf("q = %u\n", CHIPMUNK_Q);
    printf("QINV = %u\n", QINV);
    
    // Вычисляем правильное значение q^(-1) mod 2^32
    uint64_t q_inv = mod_inverse(CHIPMUNK_Q, 1ULL << 32);
    printf("Правильное q^(-1) mod 2^32 = %llu\n", q_inv);
    
    // Вычисляем -q^(-1) mod 2^32
    uint64_t neg_q_inv = ((1ULL << 32) - q_inv) & 0xFFFFFFFF;
    printf("Правильное -q^(-1) mod 2^32 = %llu\n", neg_q_inv);
    
    // Проверяем текущее значение
    uint64_t test = ((uint64_t)CHIPMUNK_Q * QINV) & 0xFFFFFFFF;
    printf("q * QINV mod 2^32 = %llu (должно быть 0 или 2^32-1)\n", test);
    
    if (test == 0xFFFFFFFF) {
        printf("✓ QINV корректно (равно -q^(-1) mod 2^32)\n");
    } else if (test == 0) {
        printf("✓ QINV корректно (равно q^(-1) mod 2^32)\n");
    } else {
        printf("✗ QINV некорректно!\n");
        printf("Должно быть: %llu\n", neg_q_inv);
    }
    
    return 0;
} 