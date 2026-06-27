/*
 * Тест правильной константы n^(-1) для N=256 и q=3168257
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "dap_common.h"
#include "chipmunk/chipmunk.h"

// Extended Euclidean Algorithm для нахождения обратного элемента
int32_t mod_inverse(int32_t a, int32_t m) {
    if (m == 1) return 0;

    int32_t m0 = m, x0 = 0, x1 = 1;

    while (a > 1) {
        int32_t q = a / m;
        int32_t t = m;
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
    printf("=== N^(-1) CALCULATION TEST ===\n\n");

    dap_common_init("chipmunk-ninv-test", NULL);

    const int32_t q = 3168257;  // CHIPMUNK_Q
    const int32_t n = CHIPMUNK_N;     // Используем реальное значение CHIPMUNK_N
    const int32_t n_orig = 512; // Оригинальное значение из Rust

    printf("🔍 Computing correct N^(-1) values:\n");

    // Вычисляем правильные значения
    int32_t n_inv_256 = mod_inverse(n, q);
    int32_t n_inv_512 = mod_inverse(n_orig, q);

    printf("- N=256: 256^(-1) mod %d = %d\n", q, n_inv_256);
    printf("- N=512: 512^(-1) mod %d = %d (original Rust)\n", q, n_inv_512);

    // Проверяем правильность
    printf("\n🔍 Verification:\n");
    printf("- (256 * %d) mod %d = %d (should be 1)\n",
           n_inv_256, q, (n * n_inv_256) % q);
    printf("- (512 * %d) mod %d = %d (should be 1)\n",
           n_inv_512, q, (n_orig * n_inv_512) % q);

    // Сравниваем с константой которую мы используем
    const int32_t used_constant = 3162069;  // Из нашего кода
    printf("\n🔍 Current constant in code: %d\n", used_constant);
    printf("- This should be for N=%d: %s\n",
           n_orig, (used_constant == n_inv_512) ? "✅ CORRECT" : "❌ WRONG");
    printf("- But we use N=%d, so we need: %d\n", n, n_inv_256);

    printf("\n🎯 CONCLUSION:\n");
    if (n_inv_256 != used_constant) {
        printf("❌ We are using WRONG n^(-1) constant!\n");
        printf("❌ Current: %d (for N=512)\n", used_constant);
        printf("✅ Should be: %d (for N=256)\n", n_inv_256);
        printf("💡 This explains the NTT/invNTT symmetry failure!\n");
        return 1;
    } else {
        printf("✅ Constant is correct!\n");
        return 0;
    }
}
