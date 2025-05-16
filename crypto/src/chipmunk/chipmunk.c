#include "chipmunk.h"
#include "chipmunk_ntt.h"
#include "chipmunk_hash.h"
#include "dap_common.h"
#include "dap_crypto_common.h"
#include "rand/dap_rand.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "chipmunk"

#define CHIPMUNK_ETA 2  // Параметр η для распределения ошибок

static int s_chipmunk_initialized = 0;

// Инициализация модуля
int chipmunk_init(void) {
    if (!s_chipmunk_initialized) {
        s_chipmunk_initialized = 1;
        log_it(L_INFO, "Chipmunk cryptographic module initialized");
    }
    return 0;
}

// Вспомогательные функции для работы с полиномами
void chipmunk_poly_ntt(chipmunk_poly_t *a) {
    chipmunk_ntt(a->coeffs);
}

void chipmunk_poly_invntt(chipmunk_poly_t *a) {
    chipmunk_invntt(a->coeffs);
}

void chipmunk_poly_add(chipmunk_poly_t *c, const chipmunk_poly_t *a, const chipmunk_poly_t *b) {
    for (int i = 0; i < CHIPMUNK_N; i++) {
        c->coeffs[i] = a->coeffs[i] + b->coeffs[i];
        if (c->coeffs[i] >= CHIPMUNK_Q)
            c->coeffs[i] -= CHIPMUNK_Q;
    }
}

void chipmunk_poly_sub(chipmunk_poly_t *c, const chipmunk_poly_t *a, const chipmunk_poly_t *b) {
    for (int i = 0; i < CHIPMUNK_N; i++) {
        c->coeffs[i] = a->coeffs[i] - b->coeffs[i];
        if (c->coeffs[i] < 0)
            c->coeffs[i] += CHIPMUNK_Q;
    }
}

void chipmunk_poly_pointwise(chipmunk_poly_t *c, const chipmunk_poly_t *a, const chipmunk_poly_t *b) {
    chipmunk_ntt_pointwise_montgomery(c->coeffs, a->coeffs, b->coeffs);
}

// Заполнение полинома равномерно распределенными коэффициентами
void chipmunk_poly_uniform(chipmunk_poly_t *a, const uint8_t seed[32], uint16_t nonce) {
    chipmunk_hash_sample_poly(a->coeffs, seed, nonce);
}

// Создание полинома challenge
void chipmunk_poly_challenge(chipmunk_poly_t *c, const uint8_t seed[32]) {
    uint8_t buf[32];
    chipmunk_hash_challenge(buf, seed, 32);
    
    // Генерируем полином с весом τ (CHIPMUNK_TAU)
    memset(c->coeffs, 0, sizeof(c->coeffs));
    
    // Устанавливаем CHIPMUNK_TAU коэффициентов в ±1
    uint8_t signs = 0;
    for (int i = 0; i < CHIPMUNK_TAU; i++) {
        uint16_t pos = ((uint16_t)buf[i*2] << 8) | buf[i*2 + 1];
        pos &= CHIPMUNK_N - 1;  // Маскировка до диапазона [0, CHIPMUNK_N-1]
        
        if (i % 8 == 0)
            signs = buf[CHIPMUNK_TAU*2 + i/8];
        
        int32_t sign = (signs & 1) ? -1 : 1;
        signs >>= 1;
        
        if (c->coeffs[pos] == 0)
            c->coeffs[pos] = sign;
        else
            i--; // Пробуем снова, если позиция уже занята
    }
}

// Проверка нормы полинома
int chipmunk_poly_chknorm(const chipmunk_poly_t *a, int32_t bound) {
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int32_t t = a->coeffs[i];
        if (t >= CHIPMUNK_Q / 2)
            t -= CHIPMUNK_Q;
        if (t < -bound || t > bound)
            return 1;  // Норма превышена
    }
    return 0;  // Норма в пределах
}

// Создание hint для верификации
static void chipmunk_make_hint(uint8_t hint[CHIPMUNK_N/8], const chipmunk_poly_t *z, const chipmunk_poly_t *r) {
    memset(hint, 0, CHIPMUNK_N/8);
    
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int32_t a = z->coeffs[i];
        int32_t b = r->coeffs[i];
        
        // Проверяем, нужен ли hint
        if (a >= CHIPMUNK_Q / 2)
            a -= CHIPMUNK_Q;
        if (b >= CHIPMUNK_Q / 2)
            b -= CHIPMUNK_Q;
        
        if ((a < 0 && b >= 0) || (a >= 0 && b < 0)) {
            hint[i/8] |= 1 << (i % 8);
        }
    }
}

// Применение hint при верификации
static void chipmunk_use_hint(chipmunk_poly_t *out, const chipmunk_poly_t *a, const uint8_t hint[CHIPMUNK_N/8]) {
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int32_t coeff = a->coeffs[i];
        if (coeff > CHIPMUNK_Q / 2)
            coeff -= CHIPMUNK_Q;
        
        // Если в позиции i бит hint установлен, меняем знак
        if ((hint[i/8] >> (i % 8)) & 1) {
            coeff = -coeff;
            if (coeff < 0)
                coeff += CHIPMUNK_Q;
        }
        
        out->coeffs[i] = coeff;
    }
}

// Генерация ключевой пары
int chipmunk_keypair(uint8_t *public_key, uint8_t *private_key) {
    if (!public_key || !private_key) {
        return -1;
    }
    
    // Инициализация, если еще не выполнена
    if (!s_chipmunk_initialized) {
        chipmunk_init();
    }
    
    // Создаем структуры для ключей
    chipmunk_private_key_t sk;
    chipmunk_public_key_t pk;
    
    // Генерируем случайный seed
    uint8_t seed[32];
    randombytes(seed, 32);
    memcpy(sk.key_seed, seed, 32);
    
    // Генерируем полиномы s1 и s2
    chipmunk_poly_uniform(&sk.s1, seed, 0);
    chipmunk_poly_uniform(&sk.s2, seed, 1);
    
    // Генерируем рандомизатор для A
    uint8_t rho[32];
    randombytes(rho, 32);
    memcpy(pk.rho.coeffs, rho, 32);
    
    // Генерируем полином A
    chipmunk_poly_t a;
    chipmunk_poly_uniform(&a, rho, 0);
    
    // Вычисляем публичный ключ h = a * s1 + s2
    chipmunk_poly_ntt(&a);
    chipmunk_poly_ntt(&sk.s1);
    chipmunk_poly_pointwise(&pk.h, &a, &sk.s1);
    chipmunk_poly_invntt(&pk.h);
    chipmunk_poly_add(&pk.h, &pk.h, &sk.s2);
    
    // Сохраняем публичный ключ в секретный ключ
    sk.pk = pk;
    
    // Вычисляем хэш публичного ключа для коммитмента
    uint8_t pk_bytes[CHIPMUNK_PUBLIC_KEY_SIZE];
    chipmunk_public_key_to_bytes(pk_bytes, &pk);
    chipmunk_hash_sha3_256(sk.tr, pk_bytes, CHIPMUNK_PUBLIC_KEY_SIZE);
    
    // Сериализуем ключи для вывода
    chipmunk_private_key_to_bytes(private_key, &sk);
    chipmunk_public_key_to_bytes(public_key, &pk);
    
    return 0;
}

// Подпись сообщения
int chipmunk_sign(const uint8_t *private_key, const uint8_t *message, size_t message_len, uint8_t *signature) {
    if (!private_key || !message || !signature) {
        return -1;
    }
    
    // Восстанавливаем секретный ключ
    chipmunk_private_key_t sk;
    if (chipmunk_private_key_from_bytes(&sk, private_key) != 0) {
        return -2;
    }
    
    // Создаем маскирующий полином y и challenge c
    chipmunk_poly_t y, c;
    chipmunk_signature_t sig;
    
    // Генерируем случайный полином y с коэффициентами в диапазоне [-γ1, γ1]
    uint8_t y_seed[32];
    randombytes(y_seed, 32);
    
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int32_t coeff = ((int32_t)y_seed[i % 32] << 8) | y_seed[(i+1) % 32];
        coeff %= (2 * CHIPMUNK_GAMMA1);
        coeff -= CHIPMUNK_GAMMA1;
        y.coeffs[i] = coeff;
    }
    
    // Восстанавливаем полином A из pk
    chipmunk_poly_t a;
    chipmunk_poly_uniform(&a, (uint8_t*)sk.pk.rho.coeffs, 0);
    chipmunk_poly_ntt(&a);
    
    // Вычисляем w = A * y
    chipmunk_poly_t w;
    chipmunk_poly_ntt(&y);
    chipmunk_poly_pointwise(&w, &a, &y);
    chipmunk_poly_invntt(&w);
    
    // Создаем challenge c
    uint8_t w_msg[CHIPMUNK_N * sizeof(int32_t) + message_len];
    memcpy(w_msg, w.coeffs, CHIPMUNK_N * sizeof(int32_t));
    memcpy(w_msg + CHIPMUNK_N * sizeof(int32_t), message, message_len);
    
    uint8_t c_seed[32];
    chipmunk_hash_challenge(c_seed, w_msg, sizeof(w_msg));
    chipmunk_poly_challenge(&c, c_seed);
    
    // Копируем c в результат
    memcpy(sig.c, c_seed, 32);
    
    // Вычисляем z = y + c * s1
    chipmunk_poly_t cs1;
    chipmunk_poly_ntt(&c);
    chipmunk_poly_pointwise(&cs1, &c, &sk.s1);
    chipmunk_poly_invntt(&cs1);
    chipmunk_poly_invntt(&y);  // Восстановление y из NTT формы
    chipmunk_poly_add(&sig.z, &y, &cs1);
    
    // Проверяем норму ||z||
    if (chipmunk_poly_chknorm(&sig.z, CHIPMUNK_GAMMA1 - CHIPMUNK_ETA)) {
        // Норма слишком большая, пробуем еще раз
        return chipmunk_sign(private_key, message, message_len, signature);
    }
    
    // Вычисляем hint
    chipmunk_poly_t r;
    chipmunk_poly_pointwise(&r, &c, &sk.s2);
    chipmunk_poly_invntt(&r);
    chipmunk_make_hint(sig.hint, &sig.z, &r);
    
    // Сериализуем подпись
    chipmunk_signature_to_bytes(signature, &sig);
    
    return 0;
}

// Проверка подписи
int chipmunk_verify(const uint8_t *public_key, const uint8_t *message, size_t message_len, const uint8_t *signature) {
    if (!public_key || !message || !signature) {
        return -1;
    }
    
    // Восстанавливаем публичный ключ
    chipmunk_public_key_t pk;
    if (chipmunk_public_key_from_bytes(&pk, public_key) != 0) {
        return -2;
    }
    
    // Восстанавливаем подпись
    chipmunk_signature_t sig;
    if (chipmunk_signature_from_bytes(&sig, signature) != 0) {
        return -3;
    }
    
    // Проверяем норму ||z||
    if (chipmunk_poly_chknorm(&sig.z, CHIPMUNK_GAMMA1 - CHIPMUNK_ETA)) {
        return -4;  // Норма слишком большая
    }
    
    // Восстанавливаем c из подписи
    chipmunk_poly_t c;
    chipmunk_poly_challenge(&c, sig.c);
    
    // Восстанавливаем A из публичного ключа
    chipmunk_poly_t a;
    chipmunk_poly_uniform(&a, (uint8_t*)pk.rho.coeffs, 0);
    
    // Вычисляем A * z - c * h = w
    chipmunk_poly_ntt(&a);
    chipmunk_poly_ntt(&sig.z);
    chipmunk_poly_ntt(&c);
    
    chipmunk_poly_t az, ch, w;
    
    chipmunk_poly_pointwise(&az, &a, &sig.z);
    chipmunk_poly_pointwise(&ch, &c, &pk.h);
    
    chipmunk_poly_invntt(&az);
    chipmunk_poly_invntt(&ch);
    
    chipmunk_poly_sub(&w, &az, &ch);
    
    // Применяем hint
    chipmunk_poly_t w_with_hint;
    chipmunk_use_hint(&w_with_hint, &w, sig.hint);
    
    // Вычисляем ожидаемый challenge
    uint8_t w_msg[CHIPMUNK_N * sizeof(int32_t) + message_len];
    memcpy(w_msg, w_with_hint.coeffs, CHIPMUNK_N * sizeof(int32_t));
    memcpy(w_msg + CHIPMUNK_N * sizeof(int32_t), message, message_len);
    
    uint8_t c_expected[32];
    chipmunk_hash_challenge(c_expected, w_msg, sizeof(w_msg));
    
    // Сравниваем полученный challenge с ожидаемым
    return memcmp(c_expected, sig.c, 32) ? -5 : 0;
}

// Сериализация публичного ключа
int chipmunk_public_key_to_bytes(uint8_t *output, const chipmunk_public_key_t *key) {
    if (!output || !key) {
        return -1;
    }
    
    // Записываем h
    for (int i = 0; i < CHIPMUNK_N; i++) {
        uint32_t coeff = key->h.coeffs[i];
        output[i*3] = coeff & 0xff;
        output[i*3 + 1] = (coeff >> 8) & 0xff;
        output[i*3 + 2] = (coeff >> 16) & 0xff;
    }
    
    // Записываем rho
    memcpy(output + CHIPMUNK_N*3, key->rho.coeffs, 32);
    
    return 0;
}

// Сериализация приватного ключа
int chipmunk_private_key_to_bytes(uint8_t *output, const chipmunk_private_key_t *key) {
    if (!output || !key) {
        return -1;
    }
    
    // Записываем s1
    for (int i = 0; i < CHIPMUNK_N; i++) {
        uint32_t coeff = key->s1.coeffs[i];
        output[i*3] = coeff & 0xff;
        output[i*3 + 1] = (coeff >> 8) & 0xff;
        output[i*3 + 2] = (coeff >> 16) & 0xff;
    }
    
    // Записываем s2
    for (int i = 0; i < CHIPMUNK_N; i++) {
        uint32_t coeff = key->s2.coeffs[i];
        output[CHIPMUNK_N*3 + i*3] = coeff & 0xff;
        output[CHIPMUNK_N*3 + i*3 + 1] = (coeff >> 8) & 0xff;
        output[CHIPMUNK_N*3 + i*3 + 2] = (coeff >> 16) & 0xff;
    }
    
    // Записываем key_seed
    memcpy(output + CHIPMUNK_N*6, key->key_seed, 32);
    
    // Записываем tr
    memcpy(output + CHIPMUNK_N*6 + 32, key->tr, 48);
    
    // Записываем публичный ключ
    return chipmunk_public_key_to_bytes(output + CHIPMUNK_N*6 + 32 + 48, &key->pk);
}

// Сериализация подписи
int chipmunk_signature_to_bytes(uint8_t *output, const chipmunk_signature_t *sig) {
    if (!output || !sig) {
        return -1;
    }
    
    // Записываем c
    memcpy(output, sig->c, 32);
    
    // Записываем z
    for (int i = 0; i < CHIPMUNK_N; i++) {
        uint32_t coeff = sig->z.coeffs[i];
        output[32 + i*3] = coeff & 0xff;
        output[32 + i*3 + 1] = (coeff >> 8) & 0xff;
        output[32 + i*3 + 2] = (coeff >> 16) & 0xff;
    }
    
    // Записываем hint
    memcpy(output + 32 + CHIPMUNK_N*3, sig->hint, CHIPMUNK_N/8);
    
    return 0;
}

// Десериализация публичного ключа
int chipmunk_public_key_from_bytes(chipmunk_public_key_t *key, const uint8_t *input) {
    if (!key || !input) {
        return -1;
    }
    
    // Читаем h
    for (int i = 0; i < CHIPMUNK_N; i++) {
        key->h.coeffs[i] = ((uint32_t)input[i*3]) | 
                          (((uint32_t)input[i*3 + 1]) << 8) | 
                          (((uint32_t)input[i*3 + 2]) << 16);
        
        if (key->h.coeffs[i] >= CHIPMUNK_Q) {
            return -2;  // Невалидный коэффициент
        }
    }
    
    // Читаем rho
    memcpy(key->rho.coeffs, input + CHIPMUNK_N*3, 32);
    
    return 0;
}

// Десериализация приватного ключа
int chipmunk_private_key_from_bytes(chipmunk_private_key_t *key, const uint8_t *input) {
    if (!key || !input) {
        return -1;
    }
    
    // Читаем s1
    for (int i = 0; i < CHIPMUNK_N; i++) {
        key->s1.coeffs[i] = ((uint32_t)input[i*3]) | 
                           (((uint32_t)input[i*3 + 1]) << 8) | 
                           (((uint32_t)input[i*3 + 2]) << 16);
        
        if (key->s1.coeffs[i] >= CHIPMUNK_Q) {
            return -2;  // Невалидный коэффициент
        }
    }
    
    // Читаем s2
    for (int i = 0; i < CHIPMUNK_N; i++) {
        key->s2.coeffs[i] = ((uint32_t)input[CHIPMUNK_N*3 + i*3]) | 
                           (((uint32_t)input[CHIPMUNK_N*3 + i*3 + 1]) << 8) | 
                           (((uint32_t)input[CHIPMUNK_N*3 + i*3 + 2]) << 16);
        
        if (key->s2.coeffs[i] >= CHIPMUNK_Q) {
            return -3;  // Невалидный коэффициент
        }
    }
    
    // Читаем key_seed
    memcpy(key->key_seed, input + CHIPMUNK_N*6, 32);
    
    // Читаем tr
    memcpy(key->tr, input + CHIPMUNK_N*6 + 32, 48);
    
    // Читаем публичный ключ
    return chipmunk_public_key_from_bytes(&key->pk, input + CHIPMUNK_N*6 + 32 + 48);
}

// Десериализация подписи
int chipmunk_signature_from_bytes(chipmunk_signature_t *sig, const uint8_t *input) {
    if (!sig || !input) {
        return -1;
    }
    
    // Читаем c
    memcpy(sig->c, input, 32);
    
    // Читаем z
    for (int i = 0; i < CHIPMUNK_N; i++) {
        sig->z.coeffs[i] = ((uint32_t)input[32 + i*3]) | 
                          (((uint32_t)input[32 + i*3 + 1]) << 8) | 
                          (((uint32_t)input[32 + i*3 + 2]) << 16);
        
        if (sig->z.coeffs[i] >= CHIPMUNK_Q) {
            return -2;  // Невалидный коэффициент
        }
    }
    
    // Читаем hint
    memcpy(sig->hint, input + 32 + CHIPMUNK_N*3, CHIPMUNK_N/8);
    
    return 0;
} 