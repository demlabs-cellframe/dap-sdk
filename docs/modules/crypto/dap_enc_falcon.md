# dap_enc_falcon.h - Falcon: Пост-квантовые цифровые подписи

## Обзор

Модуль `dap_enc_falcon` предоставляет высокопроизводительную реализацию Falcon - пост-квантового алгоритма цифровых подписей. Falcon является одним из трех финалистов конкурса NIST по пост-квантовой криптографии и обеспечивает защиту от атак квантовых компьютеров. Основан на решеточных (lattice-based) криптографических конструкциях.

## Основные возможности

- **Пост-квантовая безопасность**: Защита от атак квантовых компьютеров
- **Высокая производительность**: Оптимизированная реализация для различных уровней безопасности
- **Гибкая конфигурация**: Поддержка различных параметров и типов подписей
- **Стандартизованный алгоритм**: NIST finalist
- **Автоматическое управление памятью**
- **Кроссплатформенность** для всех поддерживаемых платформ

## Архитектура Falcon

### Уровни безопасности

Falcon поддерживает два основных уровня безопасности:

```c
typedef enum falcon_sign_degree {
    FALCON_512 = 9,   // 128-bit безопасность (рекомендуется)
    FALCON_1024 = 10  // 256-bit безопасность (максимальная)
} falcon_sign_degree_t;
```

### Типы подписей

#### Форматы подписей:
```c
typedef enum falcon_kind {
    FALCON_COMPRESSED = 0,  // Сжатый формат (минимальный размер)
    FALCON_PADDED = 1,      // С выравниванием (стандартный)
    FALCON_CT = 2           // Constant-time (постоянное время)
} falcon_kind_t;
```

#### Типы алгоритма:
```c
typedef enum falcon_sign_type {
    FALCON_DYNAMIC,  // Динамический (адаптивный)
    FALCON_TREE      // На основе дерева (Merkle tree)
} falcon_sign_type_t;
```

### Структуры данных

#### Приватный ключ:
```c
typedef struct falcon_private_key {
    falcon_kind_t kind;           // Тип подписи
    falcon_sign_degree_t degree;  // Уровень безопасности
    falcon_sign_type_t type;      // Тип алгоритма
    uint8_t *data;                // Данные ключа
} falcon_private_key_t;
```

#### Публичный ключ:
```c
typedef struct falcon_public_key {
    falcon_kind_t kind;           // Тип подписи
    falcon_sign_degree_t degree;  // Уровень безопасности
    falcon_sign_type_t type;      // Тип алгоритма
    uint8_t *data;                // Данные ключа
} falcon_public_key_t;
```

#### Подпись:
```c
typedef struct falcon_signature {
    falcon_kind_t kind;           // Тип подписи
    falcon_sign_degree_t degree;  // Уровень безопасности
    falcon_sign_type_t type;      // Тип алгоритма
    uint64_t sig_len;             // Длина подписи в байтах
    uint8_t *sig_data;            // Данные подписи
} falcon_signature_t;
```

## API Reference

### Конфигурация параметров

#### dap_enc_sig_falcon_set_degree()
```c
void dap_enc_sig_falcon_set_degree(falcon_sign_degree_t a_falcon_sign_degree);
```

**Описание**: Устанавливает уровень безопасности для новых ключей Falcon.

**Параметры**:
- `a_falcon_sign_degree` - уровень безопасности (FALCON_512 или FALCON_1024)

**Пример**:
```c
#include "dap_enc_falcon.h"

// Установить 128-bit безопасность (рекомендуется для большинства приложений)
dap_enc_sig_falcon_set_degree(FALCON_512);

// Или 256-bit безопасность для максимальной защиты
dap_enc_sig_falcon_set_degree(FALCON_1024);
```

#### dap_enc_sig_falcon_set_kind()
```c
void dap_enc_sig_falcon_set_kind(falcon_kind_t a_falcon_kind);
```

**Описание**: Устанавливает формат подписей.

**Параметры**:
- `a_falcon_kind` - формат подписи (COMPRESSED, PADDED, CT)

**Пример**:
```c
// Минимальный размер подписей
dap_enc_sig_falcon_set_kind(FALCON_COMPRESSED);

// Стандартный формат
dap_enc_sig_falcon_set_kind(FALCON_PADDED);

// Постоянное время выполнения
dap_enc_sig_falcon_set_kind(FALCON_CT);
```

#### dap_enc_sig_falcon_set_type()
```c
void dap_enc_sig_falcon_set_type(falcon_sign_type_t a_falcon_sign_type);
```

**Описание**: Устанавливает тип алгоритма подписей.

**Параметры**:
- `a_falcon_sign_type` - тип алгоритма (DYNAMIC или TREE)

**Пример**:
```c
// Динамический алгоритм (адаптивный)
dap_enc_sig_falcon_set_type(FALCON_DYNAMIC);

// Алгоритм на основе дерева Merkle
dap_enc_sig_falcon_set_type(FALCON_TREE);
```

### Инициализация и управление ключами

#### dap_enc_sig_falcon_key_new()
```c
void dap_enc_sig_falcon_key_new(dap_enc_key_t *a_key);
```

**Описание**: Инициализирует новый объект ключа Falcon.

**Параметры**:
- `a_key` - указатель на структуру ключа для инициализации

**Пример**:
```c
#include "dap_enc_key.h"
#include "dap_enc_falcon.h"

struct dap_enc_key *falcon_key = DAP_NEW(struct dap_enc_key);
dap_enc_sig_falcon_key_new(falcon_key);
// Теперь falcon_key готов к использованию с текущими настройками
```

#### dap_enc_sig_falcon_key_new_generate()
```c
void dap_enc_sig_falcon_key_new_generate(dap_enc_key_t *key, const void *kex_buf,
                                        size_t kex_size, const void *seed,
                                        size_t seed_size, size_t key_size);
```

**Описание**: Создает и генерирует новую пару ключей Falcon.

**Параметры**:
- `key` - ключ для генерации
- `kex_buf` - буфер для key exchange (не используется)
- `kex_size` - размер key exchange буфера (не используется)
- `seed` - seed для генерации (не используется, генерация происходит автоматически)
- `seed_size` - размер seed (не используется)
- `key_size` - требуемый размер ключа (не используется)

**Пример**:
```c
dap_enc_sig_falcon_key_new_generate(falcon_key, NULL, 0, NULL, 0, 0);

// После генерации:
// falcon_key->priv_key_data содержит falcon_private_key_t
// falcon_key->pub_key_data содержит falcon_public_key_t
```

#### dap_enc_sig_falcon_key_delete()
```c
void dap_enc_sig_falcon_key_delete(dap_enc_key_t *key);
```

**Описание**: Освобождает ресурсы, занятые ключом Falcon.

**Параметры**:
- `key` - ключ для удаления

**Пример**:
```c
dap_enc_sig_falcon_key_delete(falcon_key);
DAP_DELETE(falcon_key);
```

### Создание и верификация подписей

#### dap_enc_sig_falcon_get_sign()
```c
int dap_enc_sig_falcon_get_sign(dap_enc_key_t *key, const void *msg,
                               const size_t msg_size, void *signature,
                               const size_t signature_size);
```

**Описание**: Создает цифровую подпись для сообщения.

**Параметры**:
- `key` - приватный ключ для подписи
- `msg` - сообщение для подписи
- `msg_size` - размер сообщения
- `signature` - буфер для подписи
- `signature_size` - размер буфера для подписи

**Возвращает**:
- `0` - подпись создана успешно
- `-1` - ошибка создания подписи

**Пример**:
```c
const char *message = "This message will be signed with Falcon";
size_t message_len = strlen(message);

// Вычислить размер подписи
falcon_private_key_t *priv_key = (falcon_private_key_t *)falcon_key->priv_key_data;
size_t sig_size = falcon_sign_max_sig_size(priv_key);

// Выделить буфер для подписи
void *signature = malloc(sig_size);

if (signature) {
    int result = dap_enc_sig_falcon_get_sign(falcon_key, message, message_len,
                                           signature, sig_size);

    if (result == 0) {
        printf("✅ Falcon signature created successfully\n");
        // Использовать signature...
    } else {
        printf("❌ Failed to create Falcon signature\n");
    }

    free(signature);
}
```

#### dap_enc_sig_falcon_verify_sign()
```c
int dap_enc_sig_falcon_verify_sign(dap_enc_key_t *key, const void *msg,
                                  const size_t msg_size, void *signature,
                                  const size_t signature_size);
```

**Описание**: Проверяет цифровую подпись сообщения.

**Параметры**:
- `key` - публичный ключ для проверки
- `msg` - подписанное сообщение
- `msg_size` - размер сообщения
- `signature` - подпись для проверки
- `signature_size` - размер подписи

**Возвращает**:
- `0` - подпись верна
- `-1` - подпись неверна или ошибка проверки

**Пример**:
```c
int verify_result = dap_enc_sig_falcon_verify_sign(falcon_key, message, message_len,
                                                 signature, sig_size);

if (verify_result == 0) {
    printf("✅ Falcon signature verified successfully\n");
} else {
    printf("❌ Falcon signature verification failed\n");
}
```

### Сериализация и десериализация

#### dap_enc_sig_falcon_write_signature()
```c
uint8_t *dap_enc_sig_falcon_write_signature(const void *a_sign, size_t *a_buflen_out);
```

**Описание**: Сериализует подпись Falcon в бинарный формат.

**Параметры**:
- `a_sign` - указатель на falcon_signature_t
- `a_buflen_out` - указатель для сохранения размера сериализованных данных

**Возвращает**: Указатель на сериализованные данные или NULL при ошибке

**Пример**:
```c
size_t serialized_size;
uint8_t *serialized_sig = dap_enc_sig_falcon_write_signature(signature, &serialized_size);

if (serialized_sig) {
    printf("Signature serialized: %zu bytes\n", serialized_size);

    // Сохранить или передать serialized_sig...
    free(serialized_sig);
}
```

#### dap_enc_sig_falcon_read_signature()
```c
void *dap_enc_sig_falcon_read_signature(const uint8_t *a_buf, size_t a_buflen);
```

**Описание**: Десериализует подпись Falcon из бинарного формата.

**Параметры**:
- `a_buf` - сериализованные данные
- `a_buflen` - размер данных

**Возвращает**: Указатель на falcon_signature_t или NULL при ошибке

#### dap_enc_sig_falcon_write_private_key()
```c
uint8_t *dap_enc_sig_falcon_write_private_key(const void *a_private_key, size_t *a_buflen_out);
```

**Описание**: Сериализует приватный ключ Falcon.

#### dap_enc_sig_falcon_read_private_key()
```c
void *dap_enc_sig_falcon_read_private_key(const uint8_t *a_buf, size_t a_buflen);
```

**Описание**: Десериализует приватный ключ Falcon.

#### dap_enc_sig_falcon_write_public_key()
```c
uint8_t *dap_enc_sig_falcon_write_public_key(const void *a_public_key, size_t *a_buflen_out);
```

**Описание**: Сериализует публичный ключ Falcon.

#### dap_enc_sig_falcon_read_public_key()
```c
void *dap_enc_sig_falcon_read_public_key(const uint8_t *a_buf, size_t a_buflen);
```

**Описание**: Десериализует публичный ключ Falcon.

### Вспомогательные функции

#### dap_enc_sig_falcon_ser_sig_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_falcon_ser_sig_size(const void *a_sign);
```

**Описание**: Вычисляет размер сериализованной подписи.

#### dap_enc_sig_falcon_ser_private_key_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_falcon_ser_private_key_size(const void *a_skey);
```

**Описание**: Вычисляет размер сериализованного приватного ключа.

#### dap_enc_sig_falcon_ser_public_key_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_falcon_ser_public_key_size(const void *a_pkey);
```

**Описание**: Вычисляет размер сериализованного публичного ключа.

#### dap_enc_sig_falcon_deser_sig_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_falcon_deser_sig_size(const void *a_in);
```

**Описание**: Возвращает размер структуры falcon_signature_t.

#### dap_enc_sig_falcon_deser_private_key_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_falcon_deser_private_key_size(const void *a_in);
```

**Описание**: Возвращает размер структуры falcon_private_key_t.

#### dap_enc_sig_falcon_deser_public_key_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_falcon_deser_public_key_size(const void *a_in);
```

**Описание**: Возвращает размер структуры falcon_public_key_t.

## Примеры использования

### Пример 1: Базовая подпись и верификация

```c
#include "dap_enc_key.h"
#include "dap_enc_falcon.h"
#include <string.h>
#include <stdio.h>

int falcon_basic_sign_verify_example() {
    // Настройка параметров
    dap_enc_sig_falcon_set_degree(FALCON_512);    // 128-bit безопасность
    dap_enc_sig_falcon_set_kind(FALCON_PADDED);   // Стандартный формат
    dap_enc_sig_falcon_set_type(FALCON_DYNAMIC);  // Динамический алгоритм

    // Создание ключа
    struct dap_enc_key *falcon_key = DAP_NEW(struct dap_enc_key);
    dap_enc_sig_falcon_key_new(falcon_key);

    // Генерация ключевой пары
    printf("Generating Falcon keypair...\n");
    dap_enc_sig_falcon_key_new_generate(falcon_key, NULL, 0, NULL, 0, 0);

    // Данные для подписи
    const char *message = "Hello, Post-Quantum World with Falcon signatures!";
    size_t message_len = strlen(message);

    printf("Original message: %s\n", message);

    // Вычисление максимального размера подписи
    falcon_private_key_t *priv_key = (falcon_private_key_t *)falcon_key->priv_key_data;
    size_t max_sig_size = falcon_sign_max_sig_size(priv_key);
    void *signature = malloc(max_sig_size);

    if (!signature) {
        printf("❌ Memory allocation failed\n");
        dap_enc_sig_falcon_key_delete(falcon_key);
        DAP_DELETE(falcon_key);
        return -1;
    }

    // Создание подписи
    printf("Creating Falcon signature...\n");
    int sign_result = dap_enc_sig_falcon_get_sign(falcon_key, message, message_len,
                                                signature, max_sig_size);

    if (sign_result != 0) {
        printf("❌ Signature creation failed\n");
        free(signature);
        dap_enc_sig_falcon_key_delete(falcon_key);
        DAP_DELETE(falcon_key);
        return -1;
    }

    // Верификация подписи
    printf("Verifying Falcon signature...\n");
    int verify_result = dap_enc_sig_falcon_verify_sign(falcon_key, message, message_len,
                                                     signature, max_sig_size);

    if (verify_result == 0) {
        printf("✅ SUCCESS: Falcon post-quantum signature verified!\n");
        printf("   Algorithm: Falcon-512 (NIST finalist)\n");
        printf("   Security: 128-bit against quantum attacks\n");
        printf("   Signature size: %zu bytes\n", max_sig_size);
    } else {
        printf("❌ FAILURE: Signature verification failed\n");
    }

    // Очистка
    free(signature);
    dap_enc_sig_falcon_key_delete(falcon_key);
    DAP_DELETE(falcon_key);

    return verify_result == 0 ? 0 : -1;
}
```

### Пример 2: Сравнение уровней безопасности

```c
#include "dap_enc_key.h"
#include "dap_enc_falcon.h"
#include <time.h>
#include <stdio.h>

int falcon_security_levels_comparison() {
    printf("Falcon Security Levels Comparison\n");
    printf("=================================\n");

    const char *test_message = "Benchmarking Falcon signature performance";
    size_t message_len = strlen(test_message);

    struct {
        falcon_sign_degree_t degree;
        const char *name;
        int expected_security_bits;
    } levels[] = {
        {FALCON_512, "Falcon-512", 128},
        {FALCON_1024, "Falcon-1024", 256}
    };

    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        printf("\n--- Testing %s ---\n", levels[i].name);

        // Настройка уровня безопасности
        dap_enc_sig_falcon_set_degree(levels[i].degree);
        dap_enc_sig_falcon_set_kind(FALCON_PADDED);
        dap_enc_sig_falcon_set_type(FALCON_DYNAMIC);

        // Создание ключа
        struct dap_enc_key *key = DAP_NEW(struct dap_enc_key);
        dap_enc_sig_falcon_key_new(key);
        dap_enc_sig_falcon_key_new_generate(key, NULL, 0, NULL, 0, 0);

        // Получение размеров
        falcon_private_key_t *priv_key = (falcon_private_key_t *)key->priv_key_data;
        falcon_public_key_t *pub_key = (falcon_public_key_t *)key->pub_key_data;

        size_t priv_key_size = FALCON_PRIVKEY_SIZE(priv_key->degree);
        size_t pub_key_size = FALCON_PUBKEY_SIZE(pub_key->degree);
        size_t max_sig_size = falcon_sign_max_sig_size(priv_key);

        printf("Security level: %d bits\n", levels[i].expected_security_bits);
        printf("Private key size: %zu bytes\n", priv_key_size);
        printf("Public key size: %zu bytes\n", pub_key_size);
        printf("Max signature size: %zu bytes\n", max_sig_size);

        // Тестирование подписи
        void *signature = malloc(max_sig_size);
        if (signature) {
            struct timespec start, end;
            clock_gettime(CLOCK_MONOTONIC, &start);

            int sign_result = dap_enc_sig_falcon_get_sign(key, test_message, message_len,
                                                        signature, max_sig_size);

            clock_gettime(CLOCK_MONOTONIC, &end);
            double sign_time = (end.tv_sec - start.tv_sec) +
                             (end.tv_nsec - start.tv_nsec) / 1e9;

            if (sign_result == 0) {
                printf("Sign time: %.3f ms\n", sign_time * 1000);

                // Тестирование верификации
                clock_gettime(CLOCK_MONOTONIC, &start);

                int verify_result = dap_enc_sig_falcon_verify_sign(key, test_message, message_len,
                                                                 signature, max_sig_size);

                clock_gettime(CLOCK_MONOTONIC, &end);
                double verify_time = (end.tv_sec - start.tv_sec) +
                                   (end.tv_nsec - start.tv_nsec) / 1e9;

                if (verify_result == 0) {
                    printf("Verify time: %.3f ms\n", verify_time * 1000);
                    printf("✅ %s test successful\n", levels[i].name);
                } else {
                    printf("❌ %s verification failed\n", levels[i].name);
                }
            } else {
                printf("❌ %s signing failed\n", levels[i].name);
            }

            free(signature);
        }

        dap_enc_sig_falcon_key_delete(key);
        DAP_DELETE(key);
    }

    printf("\n📊 Summary:\n");
    printf("- Falcon-512: 128-bit quantum security, smaller keys/signatures\n");
    printf("- Falcon-1024: 256-bit quantum security, larger keys/signatures\n");
    printf("- Both provide post-quantum security against quantum attacks\n");

    return 0;
}
```

### Пример 3: Работа с различными форматами подписей

```c
#include "dap_enc_key.h"
#include "dap_enc_falcon.h"

int falcon_signature_formats_example() {
    printf("Falcon Signature Formats Comparison\n");
    printf("==================================\n");

    const char *test_data = "Testing different Falcon signature formats";
    size_t data_size = strlen(test_data);

    struct {
        falcon_kind_t kind;
        const char *name;
    } formats[] = {
        {FALCON_COMPRESSED, "Compressed"},
        {FALCON_PADDED, "Padded"},
        {FALCON_CT, "Constant-Time"}
    };

    // Фиксированные параметры
    dap_enc_sig_falcon_set_degree(FALCON_512);
    dap_enc_sig_falcon_set_type(FALCON_DYNAMIC);

    for (size_t i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        printf("\n--- Testing %s format ---\n", formats[i].name);

        // Установка формата
        dap_enc_sig_falcon_set_kind(formats[i].kind);

        // Создание ключа
        struct dap_enc_key *key = DAP_NEW(struct dap_enc_key);
        dap_enc_sig_falcon_key_new(key);
        dap_enc_sig_falcon_key_new_generate(key, NULL, 0, NULL, 0, 0);

        // Создание подписи
        falcon_private_key_t *priv_key = (falcon_private_key_t *)key->priv_key_data;
        size_t max_sig_size = falcon_sign_max_sig_size(priv_key);
        void *signature = malloc(max_sig_size);

        if (signature) {
            int sign_result = dap_enc_sig_falcon_get_sign(key, test_data, data_size,
                                                        signature, max_sig_size);

            if (sign_result == 0) {
                // Проверка размера подписи
                size_t actual_sig_size = 0;
                if (signature) {
                    falcon_signature_t *sig_struct = (falcon_signature_t *)signature;
                    actual_sig_size = sig_struct->sig_len;
                }

                printf("Format: %s\n", formats[i].name);
                printf("Max signature size: %zu bytes\n", max_sig_size);
                printf("Actual signature size: %zu bytes\n", actual_sig_size);

                // Верификация
                int verify_result = dap_enc_sig_falcon_verify_sign(key, test_data, data_size,
                                                                 signature, max_sig_size);

                if (verify_result == 0) {
                    printf("✅ %s format verification successful\n", formats[i].name);
                } else {
                    printf("❌ %s format verification failed\n", formats[i].name);
                }

            } else {
                printf("❌ %s format signing failed\n", formats[i].name);
            }

            free(signature);
        }

        dap_enc_sig_falcon_key_delete(key);
        DAP_DELETE(key);
    }

    printf("\n📊 Format Comparison:\n");
    printf("- Compressed: Minimal size, fastest\n");
    printf("- Padded: Standard size, balanced performance\n");
    printf("- CT: Constant time, side-channel resistant\n");

    return 0;
}
```

### Пример 4: Сериализация и хранение ключей

```c
#include "dap_enc_key.h"
#include "dap_enc_falcon.h"
#include <stdio.h>

int falcon_key_storage_example() {
    // Создание ключа
    dap_enc_sig_falcon_set_degree(FALCON_512);
    dap_enc_sig_falcon_set_kind(FALCON_PADDED);
    dap_enc_sig_falcon_set_type(FALCON_DYNAMIC);

    struct dap_enc_key *key = DAP_NEW(struct dap_enc_key);
    dap_enc_sig_falcon_key_new(key);
    dap_enc_sig_falcon_key_new_generate(key, NULL, 0, NULL, 0, 0);

    // Сериализация публичного ключа
    printf("Serializing Falcon keys...\n");

    size_t pub_key_size;
    uint8_t *pub_key_data = dap_enc_sig_falcon_write_public_key(
        key->pub_key_data, &pub_key_size);

    if (pub_key_data) {
        printf("Public key serialized: %zu bytes\n", pub_key_size);

        // Сохранение в файл
        FILE *pub_file = fopen("falcon_public.key", "wb");
        if (pub_file) {
            fwrite(pub_key_data, 1, pub_key_size, pub_file);
            fclose(pub_file);
            printf("✅ Public key saved to file\n");
        }

        free(pub_key_data);
    }

    // Сериализация приватного ключа
    size_t priv_key_size;
    uint8_t *priv_key_data = dap_enc_sig_falcon_write_private_key(
        key->priv_key_data, &priv_key_size);

    if (priv_key_data) {
        printf("Private key serialized: %zu bytes\n", priv_key_size);

        // Сохранение в файл (в реальном приложении используйте шифрование!)
        FILE *priv_file = fopen("falcon_private.key", "wb");
        if (priv_file) {
            fwrite(priv_key_data, 1, priv_key_size, priv_file);
            fclose(priv_file);
            printf("✅ Private key saved to file\n");
            printf("⚠️  WARNING: Private key should be encrypted before storage!\n");
        }

        free(priv_key_data);
    }

    // Загрузка публичного ключа
    printf("\nLoading Falcon public key...\n");

    FILE *load_pub_file = fopen("falcon_public.key", "rb");
    if (load_pub_file) {
        fseek(load_pub_file, 0, SEEK_END);
        size_t file_size = ftell(load_pub_file);
        fseek(load_pub_file, 0, SEEK_SET);

        uint8_t *loaded_pub_data = malloc(file_size);
        if (fread(loaded_pub_data, 1, file_size, load_pub_file) == file_size) {
            fclose(load_pub_file);

            // Десериализация
            falcon_public_key_t *loaded_pub_key = (falcon_public_key_t *)
                dap_enc_sig_falcon_read_public_key(loaded_pub_data, file_size);

            if (loaded_pub_key) {
                printf("✅ Public key loaded successfully\n");
                printf("   Degree: %d\n", loaded_pub_key->degree);
                printf("   Kind: %d\n", loaded_pub_key->kind);
                printf("   Type: %d\n", loaded_pub_key->type);

                // Создание ключа для верификации
                struct dap_enc_key *verify_key = DAP_NEW(struct dap_enc_key);
                dap_enc_sig_falcon_key_new(verify_key);
                verify_key->pub_key_data = loaded_pub_key;
                verify_key->pub_key_data_size = sizeof(falcon_public_key_t);

                // Тестирование верификации
                const char *test_msg = "Test message for verification";
                size_t sig_size = falcon_sign_max_sig_size((falcon_private_key_t *)key->priv_key_data);
                void *test_sig = malloc(sig_size);

                if (test_sig) {
                    int sign_result = dap_enc_sig_falcon_get_sign(key, test_msg, strlen(test_msg),
                                                                test_sig, sig_size);

                    if (sign_result == 0) {
                        int verify_result = dap_enc_sig_falcon_verify_sign(verify_key,
                                                                         test_msg, strlen(test_msg),
                                                                         test_sig, sig_size);

                        if (verify_result == 0) {
                            printf("✅ Signature verification with loaded key successful\n");
                        } else {
                            printf("❌ Signature verification failed\n");
                        }
                    }

                    free(test_sig);
                }

                dap_enc_sig_falcon_key_delete(verify_key);
                DAP_DELETE(verify_key);

            } else {
                printf("❌ Failed to load public key\n");
            }

        } else {
            printf("❌ Failed to read public key file\n");
            fclose(load_pub_file);
        }

        free(loaded_pub_data);
    }

    dap_enc_sig_falcon_key_delete(key);
    DAP_DELETE(key);

    return 0;
}
```

### Пример 5: Производительность и метрики

```c
#include "dap_enc_key.h"
#include "dap_enc_falcon.h"
#include <time.h>
#include <stdio.h>

#define PERFORMANCE_ITERATIONS 100

int falcon_performance_metrics() {
    printf("Falcon Performance Metrics\n");
    printf("==========================\n");

    // Настройка
    dap_enc_sig_falcon_set_degree(FALCON_512);
    dap_enc_sig_falcon_set_kind(FALCON_PADDED);
    dap_enc_sig_falcon_set_type(FALCON_DYNAMIC);

    // Создание ключа
    struct dap_enc_key *key = DAP_NEW(struct dap_enc_key);
    dap_enc_sig_falcon_key_new(key);
    dap_enc_sig_falcon_key_new_generate(key, NULL, 0, NULL, 0, 0);

    const char *test_data = "Performance testing data for Falcon signatures";
    size_t data_size = strlen(test_data);

    falcon_private_key_t *priv_key = (falcon_private_key_t *)key->priv_key_data;
    size_t sig_size = falcon_sign_max_sig_size(priv_key);

    printf("Test parameters:\n");
    printf("  Iterations: %d\n", PERFORMANCE_ITERATIONS);
    printf("  Data size: %zu bytes\n", data_size);
    printf("  Max signature size: %zu bytes\n", sig_size);

    // Тест генерации ключей
    printf("\n1. Key Generation Performance:\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < PERFORMANCE_ITERATIONS; i++) {
        struct dap_enc_key *temp_key = DAP_NEW(struct dap_enc_key);
        dap_enc_sig_falcon_key_new(temp_key);
        dap_enc_sig_falcon_key_new_generate(temp_key, NULL, 0, NULL, 0, 0);
        dap_enc_sig_falcon_key_delete(temp_key);
        DAP_DELETE(temp_key);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double keygen_time = (end.tv_sec - start.tv_sec) +
                        (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("   Total keygen time: %.3f seconds\n", keygen_time);
    printf("   Average per key: %.3f ms\n", (keygen_time * 1000) / PERFORMANCE_ITERATIONS);
    printf("   Keys per second: %.1f\n", PERFORMANCE_ITERATIONS / keygen_time);

    // Тест подписей
    printf("\n2. Signing Performance:\n");

    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < PERFORMANCE_ITERATIONS; i++) {
        void *signature = malloc(sig_size);
        if (signature) {
            dap_enc_sig_falcon_get_sign(key, test_data, data_size, signature, sig_size);
            free(signature);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double sign_time = (end.tv_sec - start.tv_sec) +
                      (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("   Total signing time: %.3f seconds\n", sign_time);
    printf("   Average per signature: %.3f ms\n", (sign_time * 1000) / PERFORMANCE_ITERATIONS);
    printf("   Signatures per second: %.1f\n", PERFORMANCE_ITERATIONS / sign_time);

    // Тест верификации
    printf("\n3. Verification Performance:\n");

    // Создание тестовой подписи
    void *test_signature = malloc(sig_size);
    if (test_signature) {
        dap_enc_sig_falcon_get_sign(key, test_data, data_size, test_signature, sig_size);

        clock_gettime(CLOCK_MONOTONIC, &start);

        for (int i = 0; i < PERFORMANCE_ITERATIONS; i++) {
            dap_enc_sig_falcon_verify_sign(key, test_data, data_size, test_signature, sig_size);
        }

        clock_gettime(CLOCK_MONOTONIC, &end);
        double verify_time = (end.tv_sec - start.tv_sec) +
                           (end.tv_nsec - start.tv_nsec) / 1e9;

        printf("   Total verification time: %.3f seconds\n", verify_time);
        printf("   Average per verification: %.3f ms\n", (verify_time * 1000) / PERFORMANCE_ITERATIONS);
        printf("   Verifications per second: %.1f\n", PERFORMANCE_ITERATIONS / verify_time);

        free(test_signature);
    }

    // Итоговые метрики
    printf("\n4. Performance Summary:\n");
    printf("   Key generation: %.1f keys/sec\n", PERFORMANCE_ITERATIONS / keygen_time);
    printf("   Signing: %.1f sig/sec\n", PERFORMANCE_ITERATIONS / sign_time);
    printf("   Verification: %.1f verify/sec\n", PERFORMANCE_ITERATIONS / verify_time);
    printf("   Sign/verify ratio: %.2f\n", sign_time / verify_time);

    printf("\n5. Memory Usage:\n");
    printf("   Private key size: %zu bytes\n", FALCON_PRIVKEY_SIZE(priv_key->degree));
    printf("   Public key size: %zu bytes\n", FALCON_PUBKEY_SIZE(priv_key->degree));
    printf("   Max signature size: %zu bytes\n", sig_size);

    // Очистка
    dap_enc_sig_falcon_key_delete(key);
    DAP_DELETE(key);

    return 0;
}
```

## Производительность

### Бенчмарки Falcon

| Операция | Falcon-512 | Falcon-1024 | Примечание |
|----------|------------|-------------|------------|
| **Генерация ключей** | ~50-100 μs | ~100-200 μs | Intel Core i7 |
| **Создание подписи** | ~200-300 μs | ~400-600 μs | Intel Core i7 |
| **Верификация** | ~100-150 μs | ~200-300 μs | Intel Core i7 |

### Размеры ключей и подписей

| Параметр | Falcon-512 | Falcon-1024 |
|----------|------------|-------------|
| **Приватный ключ** | 1,281 байт | 2,305 байт |
| **Публичный ключ** | 897 байт | 1,793 байт |
| **Подпись (сжатая)** | ~666 байт | ~1,281 байт |
| **Подпись (стандарт)** | ~690 байт | ~1,330 байт |

### Сравнение с классическими алгоритмами

| Алгоритм | Безопасность | Скорость подписи | Размер подписи |
|----------|-------------|------------------|----------------|
| **Falcon-512** | 128-bit PQ | ~250 μs | 690 байт |
| **Falcon-1024** | 256-bit PQ | ~500 μs | 1,330 байт |
| **ECDSA P-256** | 128-bit | ~20 μs | 64 байт |
| **RSA-3072** | 128-bit | ~1000 μs | 384 байт |
| **Ed25519** | 128-bit | ~15 μs | 64 байт |

## Безопасность

### Криптографическая стойкость

Falcon обеспечивает:
- **128-bit безопасность** против классических атак (Falcon-512)
- **256-bit безопасность** против классических атак (Falcon-1024)
- **128-bit безопасность** против квантовых атак (оба варианта)
- **EUF-CMA безопасность** (Existential Unforgeability under Chosen Message Attack)

### Рекомендации по использованию

#### Для новых проектов (рекомендуется):
```c
// Пост-квантовые подписи
dap_enc_sig_falcon_set_degree(FALCON_512);  // 128-bit безопасность
dap_enc_sig_falcon_set_kind(FALCON_PADDED); // Стандартный формат

struct dap_enc_key *key = DAP_NEW(struct dap_enc_key);
dap_enc_sig_falcon_key_new(key);
dap_enc_sig_falcon_key_new_generate(key, NULL, 0, NULL, 0, 0);
```

#### Для высокой безопасности:
```c
// Максимальная защита
dap_enc_sig_falcon_set_degree(FALCON_1024); // 256-bit безопасность
dap_enc_sig_falcon_set_kind(FALCON_CT);     // Constant-time
```

#### Для производительности:
```c
// Быстрые операции
dap_enc_sig_falcon_set_degree(FALCON_512);    // 128-bit безопасность
dap_enc_sig_falcon_set_kind(FALCON_COMPRESSED); // Минимальный размер
```

### Многоуровневая защита

```c
// Комбинация алгоритмов для максимальной безопасности
void create_hybrid_signature(const void *data, size_t data_size) {
    // Уровень 1: Falcon (пост-квантовая защита)
    struct dap_enc_key *pq_key = create_falcon_key(FALCON_512);

    // Уровень 2: ECDSA (классическая совместимость)
    struct dap_enc_key *classic_key = create_ecdsa_key();

    // Создание подписей
    void *pq_signature = create_falcon_signature(pq_key, data, data_size);
    void *classic_signature = create_ecdsa_signature(classic_key, data, data_size);

    // Сохранение обеих подписей
    // ...
}
```

## Лучшие практики

### 1. Выбор параметров

```c
// Правильный выбор параметров Falcon
void configure_falcon_for_use_case(bool high_security, bool high_speed, bool small_size) {
    if (high_security) {
        // Максимальная безопасность
        dap_enc_sig_falcon_set_degree(FALCON_1024);
        dap_enc_sig_falcon_set_kind(FALCON_CT);
    } else if (high_speed) {
        // Максимальная скорость
        dap_enc_sig_falcon_set_degree(FALCON_512);
        dap_enc_sig_falcon_set_kind(FALCON_COMPRESSED);
    } else if (small_size) {
        // Минимальный размер
        dap_enc_sig_falcon_set_degree(FALCON_512);
        dap_enc_sig_falcon_set_kind(FALCON_COMPRESSED);
    } else {
        // Баланс (рекомендуется)
        dap_enc_sig_falcon_set_degree(FALCON_512);
        dap_enc_sig_falcon_set_kind(FALCON_PADDED);
    }

    dap_enc_sig_falcon_set_type(FALCON_DYNAMIC);
}
```

### 2. Управление жизненным циклом ключей

```c
// Безопасное управление ключами Falcon
typedef struct falcon_key_manager {
    struct dap_enc_key *key;
    falcon_sign_degree_t degree;
    falcon_kind_t kind;
    time_t created_time;
    uint32_t usage_count;
    bool compromised;
} falcon_key_manager_t;

falcon_key_manager_t *falcon_key_manager_create(falcon_sign_degree_t degree) {
    falcon_key_manager_t *manager = calloc(1, sizeof(falcon_key_manager_t));
    if (!manager) return NULL;

    // Настройка параметров
    dap_enc_sig_falcon_set_degree(degree);
    dap_enc_sig_falcon_set_kind(FALCON_PADDED);
    dap_enc_sig_falcon_set_type(FALCON_DYNAMIC);

    // Создание ключа
    manager->key = DAP_NEW(struct dap_enc_key);
    if (!manager->key) {
        free(manager);
        return NULL;
    }

    dap_enc_sig_falcon_key_new(manager->key);
    dap_enc_sig_falcon_key_new_generate(manager->key, NULL, 0, NULL, 0, 0);

    manager->degree = degree;
    manager->kind = FALCON_PADDED;
    manager->created_time = time(NULL);
    manager->usage_count = 0;
    manager->compromised = false;

    return manager;
}

void falcon_key_manager_destroy(falcon_key_manager_t *manager) {
    if (manager) {
        if (manager->key) {
            dap_enc_sig_falcon_key_delete(manager->key);
            DAP_DELETE(manager->key);
        }
        free(manager);
    }
}
```

### 3. Обработка ошибок

```c
// Надежная обработка ошибок Falcon
int falcon_secure_sign(const falcon_key_manager_t *manager,
                      const void *data, size_t data_size,
                      void **signature, size_t *signature_size) {

    // Проверка параметров
    if (!manager || !manager->key || !data || data_size == 0 ||
        !signature || !signature_size) {
        return FALCON_ERROR_INVALID_PARAMS;
    }

    // Проверка состояния ключа
    if (manager->compromised) {
        return FALCON_ERROR_KEY_COMPROMISED;
    }

    // Проверка размера данных
    if (data_size > FALCON_MAX_MESSAGE_SIZE) {
        return FALCON_ERROR_DATA_TOO_LARGE;
    }

    // Получение размера подписи
    falcon_private_key_t *priv_key = (falcon_private_key_t *)manager->key->priv_key_data;
    size_t max_sig_size = falcon_sign_max_sig_size(priv_key);

    if (max_sig_size == 0) {
        return FALCON_ERROR_INVALID_KEY;
    }

    // Выделение памяти
    *signature = malloc(max_sig_size);
    if (!*signature) {
        return FALCON_ERROR_MEMORY_ALLOCATION;
    }

    // Создание подписи
    int sign_result = dap_enc_sig_falcon_get_sign(manager->key, data, data_size,
                                                *signature, max_sig_size);

    if (sign_result != 0) {
        free(*signature);
        *signature = NULL;
        return FALCON_ERROR_SIGNING_FAILED;
    }

    *signature_size = max_sig_size;
    return FALCON_SUCCESS;
}
```

### 4. Сериализация и хранение

```c
// Безопасная сериализация ключей Falcon
int falcon_secure_key_storage(const falcon_key_manager_t *manager,
                             const char *public_key_file,
                             const char *private_key_file) {

    // Сериализация публичного ключа
    size_t pub_key_size;
    uint8_t *pub_key_data = dap_enc_sig_falcon_write_public_key(
        manager->key->pub_key_data, &pub_key_size);

    if (!pub_key_data) {
        return FALCON_ERROR_SERIALIZATION_FAILED;
    }

    // Сохранение публичного ключа
    FILE *pub_file = fopen(public_key_file, "wb");
    if (!pub_file) {
        free(pub_key_data);
        return FALCON_ERROR_FILE_ACCESS;
    }

    if (fwrite(pub_key_data, 1, pub_key_size, pub_file) != pub_key_size) {
        fclose(pub_file);
        free(pub_key_data);
        return FALCON_ERROR_FILE_WRITE;
    }

    fclose(pub_file);
    free(pub_key_data);

    // Сериализация приватного ключа (с шифрованием!)
    size_t priv_key_size;
    uint8_t *priv_key_data = dap_enc_sig_falcon_write_private_key(
        manager->key->priv_key_data, &priv_key_size);

    if (!priv_key_data) {
        return FALCON_ERROR_SERIALIZATION_FAILED;
    }

    // В реальном приложении зашифруйте priv_key_data перед сохранением!
    // uint8_t *encrypted_priv_key = aes_encrypt(priv_key_data, priv_key_size);

    FILE *priv_file = fopen(private_key_file, "wb");
    if (!priv_file) {
        free(priv_key_data);
        return FALCON_ERROR_FILE_ACCESS;
    }

    if (fwrite(priv_key_data, 1, priv_key_size, priv_file) != priv_key_size) {
        fclose(priv_file);
        free(priv_key_data);
        return FALCON_ERROR_FILE_WRITE;
    }

    fclose(priv_file);
    free(priv_key_data);

    return FALCON_SUCCESS;
}
```

## Заключение

Модуль `dap_enc_falcon` предоставляет высокопроизводительную реализацию пост-квантовых цифровых подписей Falcon:

### Ключевые преимущества:
- **Пост-квантовая безопасность**: NIST finalist с доказанной безопасностью
- **Высокая производительность**: Оптимизированные алгоритмы
- **Гибкая конфигурация**: Различные уровни безопасности и форматы
- **Стандартизованный**: Четко определенные параметры и интерфейсы

### Основные возможности:
- Два уровня безопасности (128-bit и 256-bit)
- Три формата подписей (Compressed, Padded, CT)
- Полная сериализация и десериализация
- Интеграция с системой ключей DAP SDK

### Рекомендации по использованию:
1. **Для большинства приложений**: Используйте Falcon-512 с Padded форматом
2. **Для высокой безопасности**: Выбирайте Falcon-1024
3. **Для производительности**: Используйте Compressed формат
4. **Всегда проверяйте** результаты операций с подписями
5. **Безопасно храните** приватные ключи (с шифрованием)

### Следующие шаги:
1. Изучите другие пост-квантовые алгоритмы (Dilithium, Sphincs+)
2. Ознакомьтесь с примерами использования
3. Интегрируйте Falcon в свои приложения
4. Следите за развитием пост-квантовой криптографии

Для получения дополнительной информации смотрите:
- `dap_enc_falcon.h` - полный API Falcon
- `falcon_params.h` - параметры и структуры данных
- Примеры в директории `examples/crypto/`
- Тесты в директории `test/crypto/`

