#ifndef CHIPMUNK_NTT_H
#define CHIPMUNK_NTT_H

#include <stdint.h>
#include "chipmunk.h"

// Параметры для NTT
#define CHIPMUNK_ZETAS_MONT_LEN 512

extern const int32_t chipmunk_zetas_mont[CHIPMUNK_ZETAS_MONT_LEN];

// Преобразование полинома в NTT форму
void chipmunk_ntt(int32_t r[CHIPMUNK_N]);

// Обратное преобразование из NTT формы
void chipmunk_invntt(int32_t r[CHIPMUNK_N]);

// Умножение двух полиномов в NTT форме
void chipmunk_ntt_pointwise_montgomery(int32_t c[CHIPMUNK_N], 
                                     const int32_t a[CHIPMUNK_N], 
                                     const int32_t b[CHIPMUNK_N]);

// Умножение на константу Монтгомери
void chipmunk_ntt_montgomery_reduce(int32_t *r);

// Редукция по модулю q
int32_t chipmunk_ntt_mod_reduce(int32_t a);

// Барретт-редукция
int32_t chipmunk_ntt_barrett_reduce(int32_t a);

// Умножение двух чисел с редукцией
int32_t chipmunk_ntt_montgomery_multiply(int32_t a, int32_t b);

// Умножение константы на 2^32 по модулю q
int32_t chipmunk_ntt_mont_factor(int32_t a);

#endif // CHIPMUNK_NTT_H 