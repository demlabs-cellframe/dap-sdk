#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "dap-sdk/crypto/src/chipmunk/chipmunk.h"

int main() {
    printf("=== Тест отладки алгоритма Chipmunk ===\n");
    
    // Инициализация
    if (chipmunk_init() != 0) {
        printf("ОШИБКА: Не удалось инициализировать Chipmunk\n");
        return 1;
    }
    printf("✓ Chipmunk инициализирован\n");
    
    // Генерация ключей
    uint8_t public_key[CHIPMUNK_PUBLIC_KEY_SIZE];
    uint8_t private_key[CHIPMUNK_PRIVATE_KEY_SIZE];
    
    printf("Генерация ключей...\n");
    int result = chipmunk_keypair(public_key, sizeof(public_key), 
                                 private_key, sizeof(private_key));
    if (result != 0) {
        printf("ОШИБКА: Не удалось сгенерировать ключи (код: %d)\n", result);
        return 1;
    }
    printf("✓ Ключи сгенерированы\n");
    
    // Тестовое сообщение
    const char *message = "Тестовое сообщение для проверки алгоритма Chipmunk";
    size_t message_len = strlen(message);
    
    // Подпись
    uint8_t signature[CHIPMUNK_SIGNATURE_SIZE];
    printf("Создание подписи...\n");
    result = chipmunk_sign(private_key, (const uint8_t*)message, message_len, signature);
    if (result != 0) {
        printf("ОШИБКА: Не удалось создать подпись (код: %d)\n", result);
        return 1;
    }
    printf("✓ Подпись создана\n");
    
    // Верификация
    printf("Проверка подписи...\n");
    result = chipmunk_verify(public_key, (const uint8_t*)message, message_len, signature);
    if (result != 0) {
        printf("ОШИБКА: Подпись не прошла проверку (код: %d)\n", result);
        return 1;
    }
    printf("✓ Подпись успешно проверена\n");
    
    // Тест с неправильным сообщением
    const char *wrong_message = "Неправильное сообщение";
    printf("Проверка с неправильным сообщением...\n");
    result = chipmunk_verify(public_key, (const uint8_t*)wrong_message, 
                           strlen(wrong_message), signature);
    if (result == 0) {
        printf("ОШИБКА: Подпись прошла проверку с неправильным сообщением!\n");
        return 1;
    }
    printf("✓ Подпись правильно отклонена для неправильного сообщения\n");
    
    printf("\n=== Все тесты пройдены успешно! ===\n");
    printf("Проблема с полиномом w исправлена.\n");
    
    return 0;
} 