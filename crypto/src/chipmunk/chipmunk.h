#ifndef CHIPMUNK_H
#define CHIPMUNK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Параметры алгоритма
#define CHIPMUNK_N           1024  // Размер решетки
#define CHIPMUNK_Q           8380417  // Модуль q
#define CHIPMUNK_D           14  // Битовая длина для усечения
#define CHIPMUNK_TAU         39  // Параметр для хэш-функции
#define CHIPMUNK_GAMMA1      ((1 << 17))  // Параметр для подписи
#define CHIPMUNK_GAMMA2      ((CHIPMUNK_Q - 1) / 32)  // Параметр для вызова
#define CHIPMUNK_K           4  // Количество полиномов в публичном ключе

// Размеры ключей и подписи
#define CHIPMUNK_PRIVATE_KEY_SIZE 2304  // Размер приватного ключа (байт)
#define CHIPMUNK_PUBLIC_KEY_SIZE  1312  // Размер публичного ключа (байт)
#define CHIPMUNK_SIGNATURE_SIZE   2701  // Размер подписи (байт)

// Структуры данных для представления полиномов и ключей
typedef struct {
    int32_t coeffs[CHIPMUNK_N];
} chipmunk_poly_t;

typedef struct {
    chipmunk_poly_t h;      // Публичный ключ (полином)
    chipmunk_poly_t rho;    // Seed для генерации a
} chipmunk_public_key_t;

typedef struct {
    chipmunk_poly_t s1;     // Первая часть секретного ключа
    chipmunk_poly_t s2;     // Вторая часть секретного ключа
    chipmunk_public_key_t pk; // Публичный ключ
    uint8_t key_seed[32];   // Seed для ключа
    uint8_t tr[48];         // Значение для коммитмента
} chipmunk_private_key_t;

typedef struct {
    uint8_t c[32];          // Вызов (challenge)
    chipmunk_poly_t z;      // Ответ (response)
    uint8_t hint[CHIPMUNK_N/8]; // Хинт для верификации
} chipmunk_signature_t;

// Инициализация алгоритма 
int chipmunk_init(void);

// Генерация ключевой пары
int chipmunk_keypair(uint8_t *public_key, uint8_t *private_key);

// Подпись сообщения
int chipmunk_sign(const uint8_t *private_key, const uint8_t *message, 
                 size_t message_len, uint8_t *signature);

// Проверка подписи
int chipmunk_verify(const uint8_t *public_key, const uint8_t *message, 
                   size_t message_len, const uint8_t *signature);

// Сериализация/десериализация
int chipmunk_public_key_to_bytes(uint8_t *output, const chipmunk_public_key_t *key);
int chipmunk_private_key_to_bytes(uint8_t *output, const chipmunk_private_key_t *key);
int chipmunk_signature_to_bytes(uint8_t *output, const chipmunk_signature_t *sig);
int chipmunk_public_key_from_bytes(chipmunk_public_key_t *key, const uint8_t *input);
int chipmunk_private_key_from_bytes(chipmunk_private_key_t *key, const uint8_t *input);
int chipmunk_signature_from_bytes(chipmunk_signature_t *sig, const uint8_t *input);

// Вспомогательные функции (могут быть скрыты в реализации)
void chipmunk_poly_ntt(chipmunk_poly_t *a);
void chipmunk_poly_invntt(chipmunk_poly_t *a);
void chipmunk_poly_add(chipmunk_poly_t *c, const chipmunk_poly_t *a, const chipmunk_poly_t *b);
void chipmunk_poly_sub(chipmunk_poly_t *c, const chipmunk_poly_t *a, const chipmunk_poly_t *b);
void chipmunk_poly_pointwise(chipmunk_poly_t *c, const chipmunk_poly_t *a, const chipmunk_poly_t *b);
void chipmunk_poly_uniform(chipmunk_poly_t *a, const uint8_t seed[32], uint16_t nonce);
void chipmunk_poly_challenge(chipmunk_poly_t *c, const uint8_t seed[32]);
int chipmunk_poly_chknorm(const chipmunk_poly_t *a, int32_t bound);

// Функции хеширования
void chipmunk_hash_to_point(uint8_t *output, const uint8_t *input, size_t inlen);
void chipmunk_hash_to_seed(uint8_t *output, const uint8_t *input, size_t inlen);

#endif // CHIPMUNK_H 