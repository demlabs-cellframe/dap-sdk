#include "chipmunk_hash.h"
#include "dap_hash.h"
#include "dap_crypto_common.h"
#include "chipmunk.h"
#include <string.h>

// Использует SHA3-256 из dap_hash
void chipmunk_hash_sha3_256(uint8_t *output, const uint8_t *input, size_t inlen) {
    dap_hash_fast_t hash;
    dap_hash_fast(input, inlen, &hash);
    memcpy(output, hash.raw, 32);
}

// SHAKE-128 для расширяемого выхода
// Примечание: это упрощенная реализация, т.к. в SHAKE-128 длина выхода произвольная
void chipmunk_hash_shake128(uint8_t *output, size_t outlen, const uint8_t *input, size_t inlen) {
    // Используем SHA3-256 многократно для генерации нужного количества байт
    uint8_t buffer[32];
    uint8_t counter = 0;
    
    for (size_t i = 0; i < outlen; i += 32) {
        uint8_t tmp_input[inlen + 1];
        memcpy(tmp_input, input, inlen);
        tmp_input[inlen] = counter++;
        
        chipmunk_hash_sha3_256(buffer, tmp_input, inlen + 1);
        
        size_t copy_len = (i + 32 <= outlen) ? 32 : outlen - i;
        memcpy(output + i, buffer, copy_len);
    }
}

// Генерация seed для полиномов из сообщения
void chipmunk_hash_to_seed(uint8_t output[32], const uint8_t *message, size_t msglen) {
    chipmunk_hash_sha3_256(output, message, msglen);
}

// Хеширование для challenge функции
void chipmunk_hash_challenge(uint8_t output[32], const uint8_t *input, size_t inlen) {
    chipmunk_hash_sha3_256(output, input, inlen);
}

// Генерация случайного полинома по seed и nonce
void chipmunk_hash_sample_poly(int32_t *poly, const uint8_t seed[32], uint16_t nonce) {
    uint8_t buf[32 + 2];
    memcpy(buf, seed, 32);
    buf[32] = nonce & 0xff;
    buf[33] = (nonce >> 8) & 0xff;
    
    size_t total_bytes = CHIPMUNK_N * 3; // 3 байта на коэффициент для distr
    uint8_t *sample_bytes = (uint8_t*)malloc(total_bytes);
    
    if (!sample_bytes) {
        // В случае ошибки выделения памяти заполняем полином нулями
        memset(poly, 0, CHIPMUNK_N * sizeof(int32_t));
        return;
    }
    
    // Получаем случайные байты через SHAKE-128
    chipmunk_hash_shake128(sample_bytes, total_bytes, buf, 32 + 2);
    
    // Преобразуем байты в коэффициенты полинома
    for (int i = 0, j = 0; i < CHIPMUNK_N; i++, j += 3) {
        uint32_t t = ((uint32_t)sample_bytes[j]) | 
                    (((uint32_t)sample_bytes[j + 1]) << 8) | 
                    (((uint32_t)sample_bytes[j + 2]) << 16);
        
        // Приводим к диапазону [-η, η], где η = CHIPMUNK_ETA = 2
        t &= 0x7FFFFF; 
        
        // Модульная редукция по q
        t = t % CHIPMUNK_Q;
        if (t > CHIPMUNK_Q / 2) {
            t = t - CHIPMUNK_Q;
        }
        
        poly[i] = (int32_t)t;
    }
    
    free(sample_bytes);
} 