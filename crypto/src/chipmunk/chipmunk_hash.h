#ifndef CHIPMUNK_HASH_H
#define CHIPMUNK_HASH_H

#include <stdint.h>
#include <stddef.h>

// Хеширование буфера в выходной буфер
void chipmunk_hash_sha3_256(uint8_t *output, const uint8_t *input, size_t inlen);

// SHAKE-128 для генерации расширяемого выхода
void chipmunk_hash_shake128(uint8_t *output, size_t outlen, const uint8_t *input, size_t inlen);

// Генерация seed для полиномов из сообщения
void chipmunk_hash_to_seed(uint8_t output[32], const uint8_t *message, size_t msglen);

// Хеширование для challenge функции
void chipmunk_hash_challenge(uint8_t output[32], const uint8_t *input, size_t inlen);

// Генерация случайного полинома
void chipmunk_hash_sample_poly(int32_t *poly, const uint8_t seed[32], uint16_t nonce);

#endif // CHIPMUNK_HASH_H 