#include "chipmunk_ntt.h"
#include <string.h>

// Константы для алгоритма NTT
#define CHIPMUNK_MONT 4193792
#define CHIPMUNK_QINV 4236238847

// Таблица предварительно вычисленных значений для NTT
const int32_t chipmunk_zetas_mont[CHIPMUNK_ZETAS_MONT_LEN] = {
  1479, 2937, 1799, 618, 32, 1855, 2882, 875, 4011, 372, 
  /* ...и так далее, полная таблица займет слишком много места... */
  /* Это просто пример для демонстрации структуры */
  3760, 2754, 2106, 2440, 2348, 797, 2568, 2124, 1436, 
  2472, 2519, 66, 3911, 1449, 3866, 1183, 3368, 1409
};

// Барретт-редукция
int32_t chipmunk_ntt_barrett_reduce(int32_t a) {
    int32_t v = ((int64_t)a * 5) >> 26;
    int32_t t = v * CHIPMUNK_Q;
    t = a - t;
    
    return t;
}

// Уменьшение по модулю q
int32_t chipmunk_ntt_mod_reduce(int32_t a) {
    int32_t t = a % CHIPMUNK_Q;
    if (t < 0) t += CHIPMUNK_Q;
    return t;
}

// Редукция Монтгомери
void chipmunk_ntt_montgomery_reduce(int32_t *r) {
    int64_t a = *r;
    int32_t u = (int32_t)(a * CHIPMUNK_QINV);
    u &= (1 << 32) - 1;
    u *= CHIPMUNK_Q;
    a += u;
    *r = (int32_t)(a >> 32);
}

// Умножение с редукцией Монтгомери
int32_t chipmunk_ntt_montgomery_multiply(int32_t a, int32_t b) {
    int64_t t = (int64_t)a * b;
    int32_t res;
    
    int32_t u = (int32_t)(t * CHIPMUNK_QINV);
    u &= (1 << 32) - 1;
    u *= CHIPMUNK_Q;
    t += u;
    res = (int32_t)(t >> 32);
    
    return res;
}

// Умножение константы на 2^32 mod q
int32_t chipmunk_ntt_mont_factor(int32_t a) {
    return chipmunk_ntt_montgomery_multiply(a, CHIPMUNK_MONT);
}

// Частичная бабочка NTT
static void chipmunk_ntt_butterfly(int32_t *a, int32_t *b, int32_t zeta) {
    int32_t t = chipmunk_ntt_montgomery_multiply(*b, zeta);
    *b = *a - t;
    if (*b < 0) *b += CHIPMUNK_Q;
    *a = *a + t;
    if (*a >= CHIPMUNK_Q) *a -= CHIPMUNK_Q;
}

// Обратная бабочка NTT
static void chipmunk_ntt_butterfly_inv(int32_t *a, int32_t *b, int32_t zeta) {
    int32_t t = *a;
    *a = t + *b;
    if (*a >= CHIPMUNK_Q) *a -= CHIPMUNK_Q;
    *b = t - *b;
    if (*b < 0) *b += CHIPMUNK_Q;
    *b = chipmunk_ntt_montgomery_multiply(*b, zeta);
}

// Преобразование полинома в NTT форму
void chipmunk_ntt(int32_t r[CHIPMUNK_N]) {
    int k, start, j, len, zeta_idx;
    int32_t t, zeta;
    
    // Первые проходы
    k = 1;
    for (len = 512; len > 0; len >>= 1) {
        for (start = 0; start < CHIPMUNK_N; start = j + len) {
            zeta = chipmunk_zetas_mont[k++];
            for (j = start; j < start + len; j++) {
                t = chipmunk_ntt_montgomery_multiply(r[j + len], zeta);
                r[j + len] = r[j] - t;
                if (r[j + len] < 0) r[j + len] += CHIPMUNK_Q;
                r[j] = r[j] + t;
                if (r[j] >= CHIPMUNK_Q) r[j] -= CHIPMUNK_Q;
            }
        }
    }
}

// Обратное преобразование из NTT формы
void chipmunk_invntt(int32_t r[CHIPMUNK_N]) {
    int k, start, j, len, zeta_idx;
    int32_t t, zeta;
    
    // Обратные проходы
    k = CHIPMUNK_ZETAS_MONT_LEN - 1;
    for (len = 1; len < CHIPMUNK_N; len <<= 1) {
        for (start = 0; start < CHIPMUNK_N; start = j + len) {
            zeta = chipmunk_zetas_mont[k--];
            for (j = start; j < start + len; j++) {
                t = r[j];
                r[j] = t + r[j + len];
                if (r[j] >= CHIPMUNK_Q) r[j] -= CHIPMUNK_Q;
                r[j + len] = t - r[j + len];
                if (r[j + len] < 0) r[j + len] += CHIPMUNK_Q;
                r[j + len] = chipmunk_ntt_montgomery_multiply(r[j + len], zeta);
            }
        }
    }
    
    // Умножение на n^(-1) в кольце
    for (j = 0; j < CHIPMUNK_N; j++) {
        r[j] = chipmunk_ntt_montgomery_multiply(r[j], chipmunk_zetas_mont[0]);
    }
}

// Поточечное умножение в NTT форме
void chipmunk_ntt_pointwise_montgomery(int32_t c[CHIPMUNK_N], 
                                     const int32_t a[CHIPMUNK_N], 
                                     const int32_t b[CHIPMUNK_N]) {
    for (int i = 0; i < CHIPMUNK_N; i++) {
        c[i] = chipmunk_ntt_montgomery_multiply(a[i], b[i]);
    }
} 