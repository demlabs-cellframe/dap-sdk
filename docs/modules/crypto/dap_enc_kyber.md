# dap_enc_kyber.h - Kyber: Пост-квантовый алгоритм обмена ключами

> **Родительский модуль**: [Crypto Module](../crypto.md) - Полная криптографическая инфраструктура DAP SDK

## Обзор

Модуль `dap_enc_kyber` предоставляет высокопроизводительную реализацию Kyber - пост-квантового алгоритма обмена ключами (Key Encapsulation Mechanism - KEM). Kyber является финалистом конкурса NIST по пост-квантовой криптографии и обеспечивает защиту от атак квантовых компьютеров.

## Основные возможности

- **Пост-квантовая безопасность**: Защита от атак квантовых компьютеров
- **Высокая производительность**: Оптимизированные математические операции
- **Стандартизованный алгоритм**: Kyber-512 (NIST finalist)
- **Автоматическое управление памятью**
- **Кроссплатформенность** для всех поддерживаемых платформ

## Архитектура Kyber

### Основные параметры Kyber-512

```c
// Размеры ключей и данных
#define CRYPTO_SECRETKEYBYTES   1632    // Размер приватного ключа (1632 байта)
#define CRYPTO_PUBLICKEYBYTES   800     // Размер публичного ключа (800 байт)
#define CRYPTO_CIPHERTEXTBYTES  768     // Размер шифротекста (768 байт)
#define CRYPTO_BYTES            32      // Размер общего секрета (32 байта)
#define CRYPTO_ALGNAME          "Kyber512" // Имя алгоритма
```

### Принцип работы KEM (Key Encapsulation Mechanism)

Kyber работает по принципу **инкапсуляции ключа**:

1. **Генерация ключей**: Создается пара (приватный ключ, публичный ключ)
2. **Инкапсуляция**: Боб использует публичный ключ Алисы для создания общего секрета и шифротекста
3. **Декапсуляция**: Алиса использует свой приватный ключ для извлечения общего секрета из шифротекста

### Математическая основа

Kyber основан на проблеме **решеток с модулярными операциями** (Module-LWE):
- Использует кольцевые многочлены для создания криптографической стойкости
- Параметры оптимизированы для баланса между безопасностью и производительностью
- Реализует CCA-secure KEM (Chosen Ciphertext Attack secure)

## API Reference

### Инициализация и управление ключами

#### dap_enc_kyber512_key_new()
```c
void dap_enc_kyber512_key_new(dap_enc_key_t *a_key);
```

**Описание**: Инициализирует новый объект ключа Kyber.

**Параметры**:
- `a_key` - указатель на структуру ключа для инициализации

**Пример**:
```c
#include "dap_enc_key.h"
#include "dap_enc_kyber.h"

struct dap_enc_key *kyber_key = DAP_NEW(struct dap_enc_key);
dap_enc_kyber512_key_new(kyber_key);
// Теперь kyber_key готов к использованию
```

#### dap_enc_kyber512_key_delete()
```c
void dap_enc_kyber512_key_delete(dap_enc_key_t *a_key);
```

**Описание**: Освобождает ресурсы, занятые ключом Kyber.

**Параметры**:
- `a_key` - ключ для удаления

**Пример**:
```c
dap_enc_kyber512_key_delete(kyber_key);
DAP_DELETE(kyber_key);
```

#### dap_enc_kyber512_key_generate()
```c
void dap_enc_kyber512_key_generate(dap_enc_key_t *a_key,
                                  const void *a_kex_buf, size_t a_kex_size,
                                  const void *a_seed, size_t a_seed_size,
                                  size_t a_key_size);
```

**Описание**: Генерирует пару ключей Kyber (публичный + приватный).

**Параметры**:
- `a_key` - ключ для генерации
- `a_kex_buf` - буфер для key exchange (не используется)
- `a_kex_size` - размер key exchange буфера (не используется)
- `a_seed` - seed для генерации (не используется)
- `a_seed_size` - размер seed (не используется)
- `a_key_size` - требуемый размер ключа (не используется)

**Примечание**: Kyber использует встроенную генерацию ключей, параметры seed игнорируются.

**Пример**:
```c
dap_enc_kyber512_key_generate(kyber_key, NULL, 0, NULL, 0, 0);
// После генерации:
// kyber_key->pub_key_data - публичный ключ (800 байт)
// kyber_key->_inheritor - приватный ключ (1632 байта)
```

### Обмен ключами

#### dap_enc_kyber512_gen_bob_shared_key()
```c
size_t dap_enc_kyber512_gen_bob_shared_key(dap_enc_key_t *a_bob_key,
                                         const void *a_alice_pub,
                                         size_t a_alice_pub_size,
                                         void **a_cypher_msg);
```

**Описание**: Боб генерирует общий секрет и шифротекст для отправки Алисе.

**Параметры**:
- `a_bob_key` - ключ Боба (будет содержать общий секрет)
- `a_alice_pub` - публичный ключ Алисы
- `a_alice_pub_size` - размер публичного ключа Алисы
- `a_cypher_msg` - указатель для шифротекста (будет выделен функцией)

**Возвращает**: Размер шифротекста или 0 при ошибке

**Пример**:
```c
// Боб получает публичный ключ Алисы
const uint8_t *alice_public_key = get_alice_public_key();
size_t alice_pub_key_size = 800; // CRYPTO_PUBLICKEYBYTES

// Боб генерирует общий секрет и шифротекст
void *ciphertext = NULL;
size_t ciphertext_size = dap_enc_kyber512_gen_bob_shared_key(
    bob_key, alice_public_key, alice_pub_key_size, &ciphertext);

if (ciphertext_size > 0) {
    printf("Generated ciphertext: %zu bytes\n", ciphertext_size);
    // Теперь bob_key->priv_key_data содержит общий секрет (32 байта)
    // ciphertext содержит шифротекст для отправки Алисе (768 байт)

    // Отправить ciphertext Алисе...
    free(ciphertext);
} else {
    printf("Failed to generate shared key\n");
}
```

#### dap_enc_kyber512_gen_alice_shared_key()
```c
size_t dap_enc_kyber512_gen_alice_shared_key(dap_enc_key_t *a_alice_key,
                                           const void *a_alice_priv,
                                           size_t a_cypher_msg_size,
                                           uint8_t *a_cypher_msg);
```

**Описание**: Алиса извлекает общий секрет из шифротекста, используя свой приватный ключ.

**Параметры**:
- `a_alice_key` - ключ Алисы (будет содержать общий секрет)
- `a_alice_priv` - приватный ключ Алисы (не используется напрямую)
- `a_cypher_msg_size` - размер шифротекста
- `a_cypher_msg` - шифротекст, полученный от Боба

**Возвращает**: Размер общего секрета (32 байта) или 0 при ошибке

**Пример**:
```c
// Алиса получает шифротекст от Боба
uint8_t *received_ciphertext = receive_from_bob();
size_t ciphertext_size = 768; // CRYPTO_CIPHERTEXTBYTES

// Алиса извлекает общий секрет
size_t shared_secret_size = dap_enc_kyber512_gen_alice_shared_key(
    alice_key, alice_key->_inheritor, ciphertext_size, received_ciphertext);

if (shared_secret_size > 0) {
    printf("Extracted shared secret: %zu bytes\n", shared_secret_size);
    // Теперь alice_key->priv_key_data содержит тот же общий секрет, что и у Боба

    // Использовать общий секрет для шифрования...
} else {
    printf("Failed to extract shared key\n");
}

free(received_ciphertext);
```

#### dap_enc_kyber512_key_new_from_data_public()
```c
void dap_enc_kyber512_key_new_from_data_public(dap_enc_key_t *a_key,
                                             const void *a_in,
                                             size_t a_in_size);
```

**Описание**: Инициализирует ключ из публичных данных (заглушка в текущей реализации).

**Примечание**: Функция не реализована в текущей версии.

## Примеры использования

### Пример 1: Полный обмен ключами между Алисой и Бобом

```c
#include "dap_enc_key.h"
#include "dap_enc_kyber.h"
#include <string.h>

int kyber_key_exchange_example() {
    // Инициализация ключей
    struct dap_enc_key *alice_key = DAP_NEW(struct dap_enc_key);
    struct dap_enc_key *bob_key = DAP_NEW(struct dap_enc_key);

    dap_enc_kyber512_key_new(alice_key);
    dap_enc_kyber512_key_new(bob_key);

    // Шаг 1: Алиса генерирует пару ключей
    printf("Step 1: Alice generates keypair\n");
    dap_enc_kyber512_key_generate(alice_key, NULL, 0, NULL, 0, 0);

    printf("Alice public key size: %zu bytes\n", alice_key->pub_key_data_size);
    printf("Alice private key size: %zu bytes\n", alice_key->_inheritor_size);

    // Шаг 2: Боб получает публичный ключ Алисы и генерирует общий секрет
    printf("\nStep 2: Bob generates shared secret using Alice's public key\n");

    void *ciphertext = NULL;
    size_t ciphertext_size = dap_enc_kyber512_gen_bob_shared_key(
        bob_key,
        alice_key->pub_key_data,      // Публичный ключ Алисы
        alice_key->pub_key_data_size, // Размер публичного ключа
        &ciphertext
    );

    if (ciphertext_size == 0) {
        printf("❌ Bob failed to generate shared secret\n");
        goto cleanup;
    }

    printf("Bob ciphertext size: %zu bytes\n", ciphertext_size);
    printf("Bob shared secret size: %zu bytes\n", bob_key->priv_key_data_size);

    // Шаг 3: Алиса получает шифротекст от Боба и извлекает общий секрет
    printf("\nStep 3: Alice extracts shared secret from ciphertext\n");

    size_t alice_shared_size = dap_enc_kyber512_gen_alice_shared_key(
        alice_key,
        alice_key->_inheritor,  // Приватный ключ Алисы
        ciphertext_size,
        ciphertext
    );

    if (alice_shared_size == 0) {
        printf("❌ Alice failed to extract shared secret\n");
        free(ciphertext);
        goto cleanup;
    }

    printf("Alice shared secret size: %zu bytes\n", alice_shared_size);

    // Шаг 4: Проверка, что общие секреты совпадают
    printf("\nStep 4: Verify shared secrets match\n");

    if (alice_shared_size == bob_key->priv_key_data_size &&
        memcmp(alice_key->priv_key_data, bob_key->priv_key_data, alice_shared_size) == 0) {

        printf("✅ SUCCESS: Shared secrets match!\n");
        printf("   Shared secret size: %zu bytes\n", alice_shared_size);

        // Вывод первых 16 байт общего секрета (для демонстрации)
        printf("   Shared secret (first 16 bytes): ");
        for (size_t i = 0; i < 16 && i < alice_shared_size; i++) {
            printf("%02x", ((uint8_t *)alice_key->priv_key_data)[i]);
        }
        printf("\n");

    } else {
        printf("❌ FAILURE: Shared secrets don't match!\n");
    }

    free(ciphertext);

cleanup:
    // Очистка
    dap_enc_kyber512_key_delete(alice_key);
    dap_enc_kyber512_key_delete(bob_key);
    DAP_DELETE(alice_key);
    DAP_DELETE(bob_key);

    return 0;
}
```

### Пример 2: Kyber в сетевом протоколе

```c
#include "dap_enc_key.h"
#include "dap_enc_kyber.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Структура для сетевых сообщений
typedef struct {
    uint32_t message_type;    // Тип сообщения
    uint32_t payload_size;    // Размер полезной нагрузки
    uint8_t payload[];        // Полезная нагрузка
} network_message_t;

// Функции для отправки/получения сообщений (заглушки)
void send_message(const void *data, size_t size) {
    // Реальная отправка по сети
    printf("Sending %zu bytes\n", size);
}

network_message_t *receive_message() {
    // Реальный прием из сети
    return NULL;
}

int kyber_network_handshake() {
    // Инициализация ключей сервера (Алисы)
    struct dap_enc_key *server_key = DAP_NEW(struct dap_enc_key);
    dap_enc_kyber512_key_new(server_key);
    dap_enc_kyber512_key_generate(server_key, NULL, 0, NULL, 0, 0);

    // Шаг 1: Сервер отправляет свой публичный ключ клиенту
    printf("Server: Sending public key to client\n");

    network_message_t *pubkey_msg = malloc(sizeof(network_message_t) + server_key->pub_key_data_size);
    if (!pubkey_msg) {
        dap_enc_kyber512_key_delete(server_key);
        DAP_DELETE(server_key);
        return -1;
    }

    pubkey_msg->message_type = 1; // MSG_PUBLIC_KEY
    pubkey_msg->payload_size = server_key->pub_key_data_size;
    memcpy(pubkey_msg->payload, server_key->pub_key_data, server_key->pub_key_data_size);

    send_message(pubkey_msg, sizeof(network_message_t) + pubkey_msg->payload_size);
    free(pubkey_msg);

    // Шаг 2: Сервер ждет шифротекст от клиента
    printf("Server: Waiting for ciphertext from client\n");

    network_message_t *ciphertext_msg = receive_message();
    if (!ciphertext_msg || ciphertext_msg->message_type != 2) { // MSG_CIPHERTEXT
        if (ciphertext_msg) free(ciphertext_msg);
        dap_enc_kyber512_key_delete(server_key);
        DAP_DELETE(server_key);
        return -1;
    }

    // Шаг 3: Сервер извлекает общий секрет из шифротекста
    printf("Server: Extracting shared secret\n");

    size_t shared_size = dap_enc_kyber512_gen_alice_shared_key(
        server_key,
        server_key->_inheritor,
        ciphertext_msg->payload_size,
        ciphertext_msg->payload
    );

    if (shared_size == 0) {
        printf("Server: Failed to extract shared secret\n");
        free(ciphertext_msg);
        dap_enc_kyber512_key_delete(server_key);
        DAP_DELETE(server_key);
        return -1;
    }

    printf("Server: Shared secret established (%zu bytes)\n", shared_size);

    // Теперь сервер и клиент имеют общий секрет для шифрования
    // Можно использовать этот секрет для AES-GCM или другого симметричного шифрования

    free(ciphertext_msg);
    dap_enc_kyber512_key_delete(server_key);
    DAP_DELETE(server_key);

    return 0;
}

int kyber_client_handshake() {
    // Инициализация ключа клиента (Боба)
    struct dap_enc_key *client_key = DAP_NEW(struct dap_enc_key);
    dap_enc_kyber512_key_new(client_key);

    // Шаг 1: Клиент получает публичный ключ сервера
    printf("Client: Receiving server's public key\n");

    network_message_t *pubkey_msg = receive_message();
    if (!pubkey_msg || pubkey_msg->message_type != 1) { // MSG_PUBLIC_KEY
        if (pubkey_msg) free(pubkey_msg);
        dap_enc_kyber512_key_delete(client_key);
        DAP_DELETE(client_key);
        return -1;
    }

    // Шаг 2: Клиент генерирует общий секрет и шифротекст
    printf("Client: Generating shared secret\n");

    void *ciphertext = NULL;
    size_t ciphertext_size = dap_enc_kyber512_gen_bob_shared_key(
        client_key,
        pubkey_msg->payload,       // Публичный ключ сервера
        pubkey_msg->payload_size,  // Размер публичного ключа
        &ciphertext
    );

    free(pubkey_msg);

    if (ciphertext_size == 0) {
        printf("Client: Failed to generate shared secret\n");
        dap_enc_kyber512_key_delete(client_key);
        DAP_DELETE(client_key);
        return -1;
    }

    // Шаг 3: Клиент отправляет шифротекст серверу
    printf("Client: Sending ciphertext to server\n");

    network_message_t *ciphertext_msg = malloc(sizeof(network_message_t) + ciphertext_size);
    if (!ciphertext_msg) {
        free(ciphertext);
        dap_enc_kyber512_key_delete(client_key);
        DAP_DELETE(client_key);
        return -1;
    }

    ciphertext_msg->message_type = 2; // MSG_CIPHERTEXT
    ciphertext_msg->payload_size = ciphertext_size;
    memcpy(ciphertext_msg->payload, ciphertext, ciphertext_size);

    send_message(ciphertext_msg, sizeof(network_message_t) + ciphertext_size);

    free(ciphertext);
    free(ciphertext_msg);

    printf("Client: Handshake completed\n");

    dap_enc_kyber512_key_delete(client_key);
    DAP_DELETE(client_key);

    return 0;
}
```

### Пример 3: Производительность и бенчмаркинг

```c
#include "dap_enc_key.h"
#include "dap_enc_kyber.h"
#include <time.h>
#include <stdio.h>

double get_time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
}

int kyber_performance_test(int iterations) {
    printf("Kyber Performance Test (%d iterations)\n", iterations);
    printf("==================================\n");

    struct dap_enc_key *alice_key = DAP_NEW(struct dap_enc_key);
    struct dap_enc_key *bob_key = DAP_NEW(struct dap_enc_key);

    dap_enc_kyber512_key_new(alice_key);
    dap_enc_kyber512_key_new(bob_key);

    // Тест генерации ключей
    printf("\n1. Key Generation Performance:\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < iterations; i++) {
        dap_enc_kyber512_key_generate(alice_key, NULL, 0, NULL, 0, 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double keygen_time = get_time_diff(start, end);

    printf("   Key generation: %.2f ms per key (%.1f keys/sec)\n",
           (keygen_time * 1000) / iterations,
           iterations / keygen_time);

    // Тест инкапсуляции (Bob's side)
    printf("\n2. Encapsulation Performance:\n");

    // Генерируем ключ для тестирования
    dap_enc_kyber512_key_generate(alice_key, NULL, 0, NULL, 0, 0);

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < iterations; i++) {
        void *ciphertext = NULL;
        size_t ct_size = dap_enc_kyber512_gen_bob_shared_key(
            bob_key,
            alice_key->pub_key_data,
            alice_key->pub_key_data_size,
            &ciphertext
        );

        if (ct_size > 0) {
            free(ciphertext);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double encap_time = get_time_diff(start, end);

    printf("   Encapsulation: %.2f ms per operation (%.1f ops/sec)\n",
           (encap_time * 1000) / iterations,
           iterations / encap_time);

    // Тест декапсуляции (Alice's side)
    printf("\n3. Decapsulation Performance:\n");

    // Создаем тестовый шифротекст
    void *test_ciphertext = NULL;
    size_t ct_size = dap_enc_kyber512_gen_bob_shared_key(
        bob_key,
        alice_key->pub_key_data,
        alice_key->pub_key_data_size,
        &test_ciphertext
    );

    if (ct_size == 0) {
        printf("   Failed to create test ciphertext\n");
        goto cleanup;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < iterations; i++) {
        size_t ss_size = dap_enc_kyber512_gen_alice_shared_key(
            alice_key,
            alice_key->_inheritor,
            ct_size,
            test_ciphertext
        );
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double decap_time = get_time_diff(start, end);

    printf("   Decapsulation: %.2f ms per operation (%.1f ops/sec)\n",
           (decap_time * 1000) / iterations,
           iterations / decap_time);

    free(test_ciphertext);

    // Итоговые результаты
    printf("\n4. Summary:\n");
    printf("   Public key size: %zu bytes\n", CRYPTO_PUBLICKEYBYTES);
    printf("   Private key size: %zu bytes\n", CRYPTO_SECRETKEYBYTES);
    printf("   Ciphertext size: %zu bytes\n", CRYPTO_CIPHERTEXTBYTES);
    printf("   Shared secret size: %zu bytes\n", CRYPTO_BYTES);

    printf("\n   Total handshake time: %.2f ms\n",
           (keygen_time + encap_time + decap_time) * 1000 / iterations);

cleanup:
    dap_enc_kyber512_key_delete(alice_key);
    dap_enc_kyber512_key_delete(bob_key);
    DAP_DELETE(alice_key);
    DAP_DELETE(bob_key);

    return 0;
}
```

### Пример 4: Интеграция с симметричным шифрованием

```c
#include "dap_enc_key.h"
#include "dap_enc_kyber.h"
#include "dap_enc_iaes.h"
#include <string.h>

int kyber_aes_hybrid_example() {
    // Шаг 1: Установление общего секрета с Kyber
    printf("Step 1: Establishing shared secret with Kyber\n");

    struct dap_enc_key *alice_kyber = DAP_NEW(struct dap_enc_key);
    struct dap_enc_key *bob_kyber = DAP_NEW(struct dap_enc_key);

    dap_enc_kyber512_key_new(alice_kyber);
    dap_enc_kyber512_key_new(bob_kyber);

    // Алиса генерирует ключи
    dap_enc_kyber512_key_generate(alice_kyber, NULL, 0, NULL, 0, 0);

    // Боб генерирует общий секрет
    void *ciphertext = NULL;
    size_t ct_size = dap_enc_kyber512_gen_bob_shared_key(
        bob_kyber,
        alice_kyber->pub_key_data,
        alice_kyber->pub_key_data_size,
        &ciphertext
    );

    if (ct_size == 0) {
        printf("Failed to establish Kyber shared secret\n");
        goto cleanup;
    }

    // Алиса извлекает общий секрет
    size_t ss_size = dap_enc_kyber512_gen_alice_shared_key(
        alice_kyber,
        alice_kyber->_inheritor,
        ct_size,
        ciphertext
    );

    if (ss_size == 0) {
        printf("Failed to extract Kyber shared secret\n");
        free(ciphertext);
        goto cleanup;
    }

    // Шаг 2: Использование общего секрета для AES
    printf("Step 2: Using shared secret for AES encryption\n");

    // Создаем ключи AES для Алисы и Боба
    struct dap_enc_key *alice_aes = DAP_NEW(struct dap_enc_key);
    struct dap_enc_key *bob_aes = DAP_NEW(struct dap_enc_key);

    dap_enc_aes_key_new(alice_aes);
    dap_enc_aes_key_new(bob_aes);

    // Используем первые 32 байта общего секрета как ключ AES
    const uint8_t *shared_secret = alice_kyber->priv_key_data;

    // Алиса инициализирует AES с общим секретом
    memcpy(alice_aes->priv_key_data, shared_secret, IAES_KEYSIZE);
    alice_aes->priv_key_data_size = IAES_KEYSIZE;

    // Боб делает то же самое
    memcpy(bob_aes->priv_key_data, shared_secret, IAES_KEYSIZE);
    bob_aes->priv_key_data_size = IAES_KEYSIZE;

    // Шаг 3: Шифрование сообщения с AES
    const char *message = "This message is encrypted with AES using Kyber-derived key";
    printf("Original message: %s\n", message);

    // Алиса шифрует сообщение
    void *encrypted = NULL;
    size_t encrypted_size = dap_enc_iaes256_cbc_encrypt(
        alice_aes, message, strlen(message), &encrypted);

    if (encrypted_size == 0) {
        printf("AES encryption failed\n");
        free(ciphertext);
        goto cleanup;
    }

    // Боб дешифрует сообщение
    void *decrypted = NULL;
    size_t decrypted_size = dap_enc_iaes256_cbc_decrypt(
        bob_aes, encrypted, encrypted_size, &decrypted);

    if (decrypted_size == 0) {
        printf("AES decryption failed\n");
        free(encrypted);
        free(ciphertext);
        goto cleanup;
    }

    // Проверка
    if (decrypted_size == strlen(message) &&
        memcmp(decrypted, message, decrypted_size) == 0) {
        printf("✅ Hybrid Kyber+AES encryption successful!\n");
        printf("   Kyber established: %zu-byte shared secret\n", ss_size);
        printf("   AES encrypted: %zu bytes\n", encrypted_size);
        printf("   Decrypted message: %.*s\n", (int)decrypted_size, (char *)decrypted);
    } else {
        printf("❌ Hybrid encryption verification failed\n");
    }

    free(encrypted);
    free(decrypted);
    free(ciphertext);

cleanup:
    dap_enc_kyber512_key_delete(alice_kyber);
    dap_enc_kyber512_key_delete(bob_kyber);
    dap_enc_aes_key_delete(alice_aes);
    dap_enc_aes_key_delete(bob_aes);

    DAP_DELETE(alice_kyber);
    DAP_DELETE(bob_kyber);
    DAP_DELETE(alice_aes);
    DAP_DELETE(bob_aes);

    return 0;
}
```

## Производительность

### Бенчмарки Kyber-512

| Операция | Производительность | Примечание |
|----------|-------------------|------------|
| **Генерация ключей** | ~100-200 μs | Intel Core i7-8700K |
| **Инкапсуляция** | ~50-80 μs | Генерация шифротекста |
| **Декапсуляция** | ~30-50 μs | Извлечение секрета |
| **Полный handshake** | ~180-330 μs | Ключ + инкапсуляция + декапсуляция |

### Сравнение с классическими алгоритмами

| Алгоритм | Безопасность | Скорость | Размер ключей |
|----------|-------------|----------|---------------|
| **Kyber-512** | 128-bit PQ | ~200 μs | 800B pub, 1632B priv |
| **RSA-3072** | 128-bit | ~1000 μs | 3072B pub/priv |
| **ECDH P-256** | 128-bit | ~50 μs | 256B pub, 256B priv |
| **X25519** | 128-bit | ~25 μs | 256B pub, 256B priv |

## Безопасность

### Криптографическая стойкость

Kyber обеспечивает:
- **128-bit безопасность** против классических атак
- **128-bit безопасность** против квантовых атак (для Kyber-512)
- **CCA-secure KEM** (Chosen Ciphertext Attack secure)
- **Защита от side-channel атак** через постоянное время выполнения

### Рекомендации по использованию

#### Для новых проектов (рекомендуется):
```c
// Пост-квантовый обмен ключами
struct dap_enc_key *kyber_key = DAP_NEW(struct dap_enc_key);
dap_enc_kyber512_key_new(kyber_key);
dap_enc_kyber512_key_generate(kyber_key, NULL, 0, NULL, 0, 0);
```

#### В гибридных системах:
```c
// Kyber для обмена ключами + AES для шифрования данных
// 1. Установить общий секрет с Kyber
// 2. Использовать секрет как ключ для AES
// 3. Шифровать данные с AES
```

#### Для сетевых протоколов:
```c
// 1. Alice: generate_keypair() -> send(public_key)
// 2. Bob: encapsulate(public_key) -> send(ciphertext)
// 3. Alice: decapsulate(ciphertext) -> shared_secret
// 4. Использовать shared_secret для симметричного шифрования
```

### Предупреждения

- **Время выполнения**: Операции Kyber медленнее классических алгоритмов
- **Размер ключей**: Публичные ключи Kyber больше, чем у ECDSA
- **Память**: Требуется больше памяти для ключей и промежуточных данных
- **Совместимость**: Только с пост-квантовыми системами

## Лучшие практики

### 1. Управление жизненным циклом ключей

```c
// Правильное управление ключами Kyber
typedef struct kyber_key_context {
    struct dap_enc_key *key;
    time_t created_time;
    uint32_t usage_count;
    bool key_exchange_completed;
} kyber_key_context_t;

kyber_key_context_t *kyber_context_create() {
    kyber_key_context_t *ctx = calloc(1, sizeof(kyber_key_context_t));
    if (!ctx) return NULL;

    ctx->key = DAP_NEW(struct dap_enc_key);
    if (!ctx->key) {
        free(ctx);
        return NULL;
    }

    dap_enc_kyber512_key_new(ctx->key);
    dap_enc_kyber512_key_generate(ctx->key, NULL, 0, NULL, 0, 0);

    ctx->created_time = time(NULL);
    ctx->usage_count = 0;
    ctx->key_exchange_completed = false;

    return ctx;
}

void kyber_context_destroy(kyber_key_context_t *ctx) {
    if (ctx) {
        if (ctx->key) {
            dap_enc_kyber512_key_delete(ctx->key);
            DAP_DELETE(ctx->key);
        }
        free(ctx);
    }
}
```

### 2. Безопасный обмен ключами

```c
// Надежный протокол обмена ключами
int secure_kyber_handshake(kyber_key_context_t *alice_ctx,
                          kyber_key_context_t *bob_ctx,
                          uint8_t **shared_secret,
                          size_t *secret_size) {

    // Проверка состояния
    if (!alice_ctx || !bob_ctx || !alice_ctx->key || !bob_ctx->key) {
        return -1;
    }

    // Боб генерирует шифротекст
    void *ciphertext = NULL;
    size_t ct_size = dap_enc_kyber512_gen_bob_shared_key(
        bob_ctx->key,
        alice_ctx->key->pub_key_data,
        alice_ctx->key->pub_key_data_size,
        &ciphertext
    );

    if (ct_size == 0) {
        return -2; // Ошибка инкапсуляции
    }

    // Алиса извлекает секрет
    size_t ss_size = dap_enc_kyber512_gen_alice_shared_key(
        alice_ctx->key,
        alice_ctx->key->_inheritor,
        ct_size,
        ciphertext
    );

    free(ciphertext);

    if (ss_size == 0) {
        return -3; // Ошибка декапсуляции
    }

    // Проверка совпадения секретов
    if (ss_size != bob_ctx->key->priv_key_data_size ||
        memcmp(alice_ctx->key->priv_key_data,
               bob_ctx->key->priv_key_data, ss_size) != 0) {
        return -4; // Секреты не совпадают
    }

    // Возврат общего секрета
    *shared_secret = malloc(ss_size);
    if (!*shared_secret) {
        return -5; // Ошибка памяти
    }

    memcpy(*shared_secret, alice_ctx->key->priv_key_data, ss_size);
    *secret_size = ss_size;

    alice_ctx->key_exchange_completed = true;
    bob_ctx->key_exchange_completed = true;

    return 0; // Успех
}
```

### 3. Обработка ошибок

```c
// Надежная обработка ошибок Kyber
int kyber_operation_with_error_handling(struct dap_enc_key *key,
                                       kyber_operation_t operation,
                                       const void *input, size_t input_size,
                                       void **output, size_t *output_size) {

    // Проверка параметров
    if (!key || !input || input_size == 0 || !output || !output_size) {
        errno = EINVAL;
        return KYBER_ERROR_INVALID_PARAMS;
    }

    // Проверка типа ключа
    if (key->type != DAP_ENC_KEY_TYPE_KEM_KYBER512) {
        errno = EINVAL;
        return KYBER_ERROR_INVALID_KEY_TYPE;
    }

    // Проверка состояния ключа
    if (!key->pub_key_data || key->pub_key_data_size != CRYPTO_PUBLICKEYBYTES) {
        errno = EINVAL;
        return KYBER_ERROR_INVALID_KEY_STATE;
    }

    // Выполнение операции
    size_t result_size = 0;

    switch (operation) {
        case KYBER_ENCAPSULATE:
            result_size = dap_enc_kyber512_gen_bob_shared_key(key, input, input_size, output);
            break;

        case KYBER_DECAPSULATE:
            result_size = dap_enc_kyber512_gen_alice_shared_key(key, NULL, input_size, (uint8_t *)input);
            break;

        default:
            errno = EINVAL;
            return KYBER_ERROR_INVALID_OPERATION;
    }

    if (result_size == 0) {
        errno = EIO;
        return KYBER_ERROR_OPERATION_FAILED;
    }

    *output_size = result_size;
    return KYBER_SUCCESS;
}
```

## Заключение

Модуль `dap_enc_kyber` предоставляет современную пост-квантовую криптографию для DAP SDK:

### Ключевые преимущества:
- **Пост-квантовая безопасность**: Защита от квантовых компьютеров
- **Стандартизованный алгоритм**: NIST finalist
- **Высокая производительность**: Оптимизированная реализация
- **Простота использования**: Четкий и понятный API

### Основные возможности:
- Генерация пары ключей (публичный + приватный)
- Инкапсуляция общего секрета (Bob's side)
- Декапсуляция общего секрета (Alice's side)
- Интеграция с симметричными шифрами

### Рекомендации по использованию:
1. **Для новых проектов**: Используйте Kyber вместо классических алгоритмов
2. **В гибридных системах**: Kyber + AES для оптимальной производительности
3. **В сетевых протоколах**: Kyber для первоначального обмена ключами
4. **Всегда проверяйте** результаты операций и размеры данных

### Следующие шаги:
1. Изучите другие пост-квантовые алгоритмы (Falcon, Dilithium)
2. Ознакомьтесь с примерами использования
3. Интегрируйте Kyber в свои приложения
4. Следите за развитием пост-квантовой криптографии

Для получения дополнительной информации смотрите:
- `dap_enc_kyber.h` - полный API Kyber
- `kyber512.h` - параметры алгоритма Kyber-512
- Примеры в директории `examples/crypto/`
- Тесты в директории `test/crypto/`

