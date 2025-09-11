# dap_enc_dilithium.h - Dilithium: Пост-квантовые цифровые подписи

## Обзор

Модуль `dap_enc_dilithium` предоставляет высокопроизводительную реализацию Dilithium - пост-квантового алгоритма цифровых подписей. Dilithium является одним из трех финалистов конкурса NIST по пост-квантовой криптографии и обеспечивает защиту от атак квантовых компьютеров. Основан на решеточных (lattice-based) криптографических конструкциях с использованием модулярных решеток.

## Основные возможности

- **Пост-квантовая безопасность**: Защита от атак квантовых компьютеров
- **Четыре уровня безопасности**: От минимального размера до максимальной безопасности
- **Высокая производительность**: Оптимизированные математические операции
- **Стандартизованный алгоритм**: NIST finalist
- **Автоматическое управление памятью**
- **Кроссплатформенность** для всех поддерживаемых платформ

## Архитектура Dilithium

### Уровни безопасности

Dilithium поддерживает четыре различных уровня безопасности, оптимизированных для разных сценариев использования:

```c
typedef enum DAP_DILITHIUM_SIGN_SECURITY {
    DILITHIUM_TOY = 0,           // Тестовый уровень (не для продакшена)
    DILITHIUM_MAX_SPEED,         // Максимальная скорость
    DILITHIUM_MIN_SIZE,          // Минимальный размер подписей
    DILITHIUM_MAX_SECURITY       // Максимальная безопасность
} DAP_DILITHIUM_SIGN_SECURITY;
```

### Параметры Dilithium

Каждый уровень безопасности имеет свои криптографические параметры:

#### Dilithium2 (MAX_SPEED)
- **Безопасность**: 128-bit
- **Размер подписи**: ~2,420 байт
- **Размер публичного ключа**: 1,312 байт
- **Размер приватного ключа**: 2,528 байт

#### Dilithium3 (MIN_SIZE)  
- **Безопасность**: 160-bit
- **Размер подписи**: ~3,293 байт
- **Размер публичного ключа**: 1,952 байт
- **Размер приватного ключа**: 4,000 байт

#### Dilithium4 (MAX_SECURITY)
- **Безопасность**: 192-bit
- **Размер подписи**: ~4,595 байт
- **Размер публичного ключа**: 2,592 байт
- **Размер приватного ключа**: 5,376 байт

### Структуры данных

#### Приватный ключ:
```c
typedef struct {
    dilithium_kind_t kind;      // Уровень безопасности
    unsigned char *data;        // Данные ключа
} dilithium_private_key_t;
```

#### Публичный ключ:
```c
typedef struct {
    dilithium_kind_t kind;      // Уровень безопасности
    unsigned char *data;        // Данные ключа
} dilithium_public_key_t;
```

#### Подпись:
```c
typedef struct {
    dilithium_kind_t kind;      // Уровень безопасности
    unsigned char *sig_data;    // Данные подписи
    uint64_t sig_len;           // Длина подписи в байтах
} dilithium_signature_t;
```

## API Reference

### Конфигурация параметров

#### dap_enc_sig_dilithium_set_type()
```c
void dap_enc_sig_dilithium_set_type(enum DAP_DILITHIUM_SIGN_SECURITY type);
```

**Описание**: Устанавливает уровень безопасности для новых ключей Dilithium.

**Параметры**:
- `type` - уровень безопасности (TOY, MAX_SPEED, MIN_SIZE, MAX_SECURITY)

**Пример**:
```c
#include "dap_enc_dilithium.h"

// Максимальная скорость (рекомендуется для большинства приложений)
dap_enc_sig_dilithium_set_type(DILITHIUM_MAX_SPEED);

// Минимальный размер подписей
dap_enc_sig_dilithium_set_type(DILITHIUM_MIN_SIZE);

// Максимальная безопасность
dap_enc_sig_dilithium_set_type(DILITHIUM_MAX_SECURITY);

// Тестовый уровень (только для тестирования)
dap_enc_sig_dilithium_set_type(DILITHIUM_TOY);
```

### Инициализация и управление ключами

#### dap_enc_sig_dilithium_key_new()
```c
void dap_enc_sig_dilithium_key_new(dap_enc_key_t *a_key);
```

**Описание**: Инициализирует новый объект ключа Dilithium.

**Параметры**:
- `a_key` - указатель на структуру ключа для инициализации

**Пример**:
```c
#include "dap_enc_key.h"
#include "dap_enc_dilithium.h"

struct dap_enc_key *dilithium_key = DAP_NEW(struct dap_enc_key);
dap_enc_sig_dilithium_key_new(dilithium_key);
// Теперь dilithium_key готов к использованию
```

#### dap_enc_sig_dilithium_key_new_generate()
```c
void dap_enc_sig_dilithium_key_new_generate(dap_enc_key_t *key, const void *kex_buf,
                                          size_t kex_size, const void *seed,
                                          size_t seed_size, size_t key_size);
```

**Описание**: Создает и генерирует новую пару ключей Dilithium.

**Параметры**:
- `key` - ключ для генерации
- `kex_buf` - буфер для key exchange (не используется)
- `kex_size` - размер key exchange буфера (не используется)
- `seed` - seed для генерации ключа (опционально)
- `seed_size` - размер seed
- `key_size` - требуемый размер ключа (не используется)

**Пример**:
```c
// Генерация с seed для воспроизводимости
const char *seed = "my_deterministic_seed";
dap_enc_sig_dilithium_key_new_generate(dilithium_key, NULL, 0, seed, strlen(seed), 0);

// Или генерация полностью случайная
dap_enc_sig_dilithium_key_new_generate(dilithium_key, NULL, 0, NULL, 0, 0);

// После генерации:
// dilithium_key->priv_key_data содержит dilithium_private_key_t
// dilithium_key->pub_key_data содержит dilithium_public_key_t
```

#### dap_enc_sig_dilithium_key_delete()
```c
void dap_enc_sig_dilithium_key_delete(dap_enc_key_t *a_key);
```

**Описание**: Освобождает ресурсы, занятые ключом Dilithium.

**Параметры**:
- `a_key` - ключ для удаления

**Пример**:
```c
dap_enc_sig_dilithium_key_delete(dilithium_key);
DAP_DELETE(dilithium_key);
```

### Создание и верификация подписей

#### dap_enc_sig_dilithium_get_sign()
```c
int dap_enc_sig_dilithium_get_sign(dap_enc_key_t *a_key, const void *a_msg,
                                 const size_t a_msg_size, void *a_sig,
                                 const size_t a_sig_size);
```

**Описание**: Создает цифровую подпись для сообщения.

**Параметры**:
- `a_key` - приватный ключ для подписи
- `a_msg` - сообщение для подписи
- `a_msg_size` - размер сообщения
- `a_sig` - буфер для подписи
- `a_sig_size` - размер буфера для подписи (должен быть >= sizeof(dilithium_signature_t))

**Возвращает**:
- `0` - подпись создана успешно
- `-1` - ошибка создания подписи

**Пример**:
```c
const char *message = "This message will be signed with Dilithium";
size_t message_len = strlen(message);

// Выделить буфер для подписи
void *signature = malloc(sizeof(dilithium_signature_t));

if (signature) {
    int sign_result = dap_enc_sig_dilithium_get_sign(dilithium_key,
                                                   message, message_len,
                                                   signature, sizeof(dilithium_signature_t));

    if (sign_result == 0) {
        printf("✅ Dilithium signature created successfully\n");
        // Использовать signature...
    } else {
        printf("❌ Failed to create Dilithium signature\n");
    }

    free(signature);
}
```

#### dap_enc_sig_dilithium_verify_sign()
```c
int dap_enc_sig_dilithium_verify_sign(dap_enc_key_t *a_key, const void *a_msg,
                                    const size_t a_msg_size, void *a_sig,
                                    const size_t a_sig_size);
```

**Описание**: Проверяет цифровую подпись сообщения.

**Параметры**:
- `a_key` - публичный ключ для проверки
- `a_msg` - подписанное сообщение
- `a_msg_size` - размер сообщения
- `a_sig` - подпись для проверки
- `a_sig_size` - размер подписи (должен быть >= sizeof(dilithium_signature_t))

**Возвращает**:
- `0` - подпись верна
- `-1` - подпись неверна или ошибка проверки

**Пример**:
```c
int verify_result = dap_enc_sig_dilithium_verify_sign(dilithium_key,
                                                    message, message_len,
                                                    signature, sizeof(dilithium_signature_t));

if (verify_result == 0) {
    printf("✅ Dilithium signature verified successfully\n");
} else {
    printf("❌ Dilithium signature verification failed\n");
}
```

### Сериализация и десериализация

#### dap_enc_sig_dilithium_write_signature()
```c
uint8_t *dap_enc_sig_dilithium_write_signature(const void *a_sign, size_t *a_buflen_out);
```

**Описание**: Сериализует подпись Dilithium в бинарный формат.

**Параметры**:
- `a_sign` - указатель на dilithium_signature_t
- `a_buflen_out` - указатель для сохранения размера сериализованных данных

**Возвращает**: Указатель на сериализованные данные или NULL при ошибке

**Пример**:
```c
size_t serialized_size;
uint8_t *serialized_sig = dap_enc_sig_dilithium_write_signature(signature, &serialized_size);

if (serialized_sig) {
    printf("Signature serialized: %zu bytes\n", serialized_size);

    // Сохранить или передать serialized_sig...
    free(serialized_sig);
}
```

#### dap_enc_sig_dilithium_read_signature()
```c
void *dap_enc_sig_dilithium_read_signature(const uint8_t *a_buf, size_t a_buflen);
```

**Описание**: Десериализует подпись Dilithium из бинарного формата.

**Параметры**:
- `a_buf` - сериализованные данные
- `a_buflen` - размер данных

**Возвращает**: Указатель на dilithium_signature_t или NULL при ошибке

#### dap_enc_sig_dilithium_write_private_key()
```c
uint8_t *dap_enc_sig_dilithium_write_private_key(const void *a_private_key, size_t *a_buflen_out);
```

**Описание**: Сериализует приватный ключ Dilithium.

#### dap_enc_sig_dilithium_read_private_key()
```c
void *dap_enc_sig_dilithium_read_private_key(const uint8_t *a_buf, size_t a_buflen);
```

**Описание**: Десериализует приватный ключ Dilithium.

#### dap_enc_sig_dilithium_write_public_key()
```c
uint8_t *dap_enc_sig_dilithium_write_public_key(const void *a_public_key, size_t *a_buflen_out);
```

**Описание**: Сериализует публичный ключ Dilithium.

#### dap_enc_sig_dilithium_read_public_key()
```c
void *dap_enc_sig_dilithium_read_public_key(const uint8_t *a_buf, size_t a_buflen);
```

**Описание**: Десериализует публичный ключ Dilithium.

### Вспомогательные функции

#### dap_enc_sig_dilithium_ser_sig_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_dilithium_ser_sig_size(const void *a_sign);
```

**Описание**: Вычисляет размер сериализованной подписи.

#### dap_enc_sig_dilithium_ser_private_key_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_dilithium_ser_private_key_size(const void *a_skey);
```

**Описание**: Вычисляет размер сериализованного приватного ключа.

#### dap_enc_sig_dilithium_ser_public_key_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_dilithium_ser_public_key_size(const void *a_pkey);
```

**Описание**: Вычисляет размер сериализованного публичного ключа.

#### dap_enc_sig_dilithium_deser_sig_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_dilithium_deser_sig_size(const void *a_in);
```

**Описание**: Возвращает размер структуры dilithium_signature_t.

#### dap_enc_sig_dilithium_deser_private_key_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_dilithium_deser_private_key_size(const void *a_in);
```

**Описание**: Возвращает размер структуры dilithium_private_key_t.

#### dap_enc_sig_dilithium_deser_public_key_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_dilithium_deser_public_key_size(const void *a_in);
```

**Описание**: Возвращает размер структуры dilithium_public_key_t.

## Примеры использования

### Пример 1: Базовая подпись и верификация

```c
#include "dap_enc_key.h"
#include "dap_enc_dilithium.h"
#include <string.h>
#include <stdio.h>

int dilithium_basic_sign_verify_example() {
    // Настройка уровня безопасности
    dap_enc_sig_dilithium_set_type(DILITHIUM_MAX_SPEED); // 128-bit безопасность

    // Создание ключа
    struct dap_enc_key *dilithium_key = DAP_NEW(struct dap_enc_key);
    dap_enc_sig_dilithium_key_new(dilithium_key);

    // Генерация ключевой пары
    printf("Generating Dilithium keypair...\n");
    dap_enc_sig_dilithium_key_new_generate(dilithium_key, NULL, 0, NULL, 0, 0);

    // Данные для подписи
    const char *message = "Hello, Post-Quantum World with Dilithium signatures!";
    size_t message_len = strlen(message);

    printf("Original message: %s\n", message);
    printf("Message length: %zu bytes\n", message_len);

    // Создание подписи
    printf("Creating Dilithium signature...\n");
    void *signature = malloc(sizeof(dilithium_signature_t));

    if (!signature) {
        printf("❌ Memory allocation failed\n");
        dap_enc_sig_dilithium_key_delete(dilithium_key);
        DAP_DELETE(dilithium_key);
        return -1;
    }

    int sign_result = dap_enc_sig_dilithium_get_sign(dilithium_key, message, message_len,
                                                   signature, sizeof(dilithium_signature_t));

    if (sign_result != 0) {
        printf("❌ Signature creation failed\n");
        free(signature);
        dap_enc_sig_dilithium_key_delete(dilithium_key);
        DAP_DELETE(dilithium_key);
        return -1;
    }

    // Получение информации о подписи
    dilithium_signature_t *sig_struct = (dilithium_signature_t *)signature;
    printf("Signature created: %zu bytes\n", sig_struct->sig_len);

    // Верификация подписи
    printf("Verifying Dilithium signature...\n");
    int verify_result = dap_enc_sig_dilithium_verify_sign(dilithium_key, message, message_len,
                                                        signature, sizeof(dilithium_signature_t));

    if (verify_result == 0) {
        printf("✅ SUCCESS: Dilithium post-quantum signature verified!\n");
        printf("   Algorithm: Dilithium (NIST finalist)\n");
        printf("   Security level: 128-bit against quantum attacks\n");
        printf("   Signature size: %zu bytes\n", sig_struct->sig_len);
    } else {
        printf("❌ FAILURE: Signature verification failed\n");
    }

    // Очистка
    free(signature);
    dap_enc_sig_dilithium_key_delete(dilithium_key);
    DAP_DELETE(dilithium_key);

    return verify_result == 0 ? 0 : -1;
}
```

### Пример 2: Сравнение уровней безопасности

```c
#include "dap_enc_key.h"
#include "dap_enc_dilithium.h"
#include <time.h>
#include <stdio.h>

int dilithium_security_levels_comparison() {
    printf("Dilithium Security Levels Comparison\n");
    printf("=====================================\n");

    const char *test_message = "Benchmarking Dilithium signature performance";
    size_t message_len = strlen(test_message);

    struct {
        enum DAP_DILITHIUM_SIGN_SECURITY level;
        const char *name;
        int expected_security_bits;
    } levels[] = {
        {DILITHIUM_MAX_SPEED, "Dilithium2 (Max Speed)", 128},
        {DILITHIUM_MIN_SIZE, "Dilithium3 (Min Size)", 160},
        {DILITHIUM_MAX_SECURITY, "Dilithium4 (Max Security)", 192}
    };

    for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        printf("\n--- Testing %s ---\n", levels[i].name);

        // Установка уровня безопасности
        dap_enc_sig_dilithium_set_type(levels[i].level);

        // Создание ключа
        struct dap_enc_key *key = DAP_NEW(struct dap_enc_key);
        dap_enc_sig_dilithium_key_new(key);
        dap_enc_sig_dilithium_key_new_generate(key, NULL, 0, NULL, 0, 0);

        // Получение размеров
        dilithium_private_key_t *priv_key = (dilithium_private_key_t *)key->priv_key_data;
        dilithium_public_key_t *pub_key = (dilithium_public_key_t *)key->pub_key_data;

        // Вычисление размеров ключей
        dilithium_param_t params;
        dilithium_params_init(&params, priv_key->kind);

        printf("Security level: %d bits\n", levels[i].expected_security_bits);
        printf("Public key size: %u bytes\n", params.CRYPTO_PUBLICKEYBYTES);
        printf("Private key size: %u bytes\n", params.CRYPTO_SECRETKEYBYTES);
        printf("Max signature size: %u bytes\n", params.CRYPTO_BYTES);

        // Тестирование подписи
        void *signature = malloc(sizeof(dilithium_signature_t));
        if (signature) {
            struct timespec start, end;
            clock_gettime(CLOCK_MONOTONIC, &start);

            int sign_result = dap_enc_sig_dilithium_get_sign(key, test_message, message_len,
                                                           signature, sizeof(dilithium_signature_t));

            clock_gettime(CLOCK_MONOTONIC, &end);
            double sign_time = (end.tv_sec - start.tv_sec) +
                             (end.tv_nsec - start.tv_nsec) / 1e9;

            if (sign_result == 0) {
                printf("Sign time: %.3f ms\n", sign_time * 1000);

                // Тестирование верификации
                clock_gettime(CLOCK_MONOTONIC, &start);

                int verify_result = dap_enc_sig_dilithium_verify_sign(key, test_message, message_len,
                                                                   signature, sizeof(dilithium_signature_t));

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

        dap_enc_sig_dilithium_key_delete(key);
        DAP_DELETE(key);
    }

    printf("\n📊 Summary:\n");
    printf("- Dilithium2: 128-bit quantum security, fastest\n");
    printf("- Dilithium3: 160-bit quantum security, balanced\n");
    printf("- Dilithium4: 192-bit quantum security, most secure\n");
    printf("- All provide post-quantum security against quantum attacks\n");

    return 0;
}
```

### Пример 3: Детерминированная генерация ключей

```c
#include "dap_enc_key.h"
#include "dap_enc_dilithium.h"
#include <string.h>
#include <stdio.h>

int dilithium_deterministic_keys_example() {
    printf("Dilithium Deterministic Key Generation\n");
    printf("======================================\n");

    const char *seed1 = "deterministic_seed_alice";
    const char *seed2 = "deterministic_seed_alice"; // Тот же seed
    const char *seed3 = "deterministic_seed_bob";   // Другой seed

    // Создание ключей с одинаковым seed
    struct dap_enc_key *key1 = DAP_NEW(struct dap_enc_key);
    struct dap_enc_key *key2 = DAP_NEW(struct dap_enc_key);
    struct dap_enc_key *key3 = DAP_NEW(struct dap_enc_key);

    dap_enc_sig_dilithium_key_new(key1);
    dap_enc_sig_dilithium_key_new(key2);
    dap_enc_sig_dilithium_key_new(key3);

    // Генерация ключей
    dap_enc_sig_dilithium_key_new_generate(key1, NULL, 0, seed1, strlen(seed1), 0);
    dap_enc_sig_dilithium_key_new_generate(key2, NULL, 0, seed2, strlen(seed2), 0);
    dap_enc_sig_dilithium_key_new_generate(key3, NULL, 0, seed3, strlen(seed3), 0);

    // Сравнение публичных ключей
    dilithium_public_key_t *pub1 = (dilithium_public_key_t *)key1->pub_key_data;
    dilithium_public_key_t *pub2 = (dilithium_public_key_t *)key2->pub_key_data;
    dilithium_public_key_t *pub3 = (dilithium_public_key_t *)key3->pub_key_data;

    // Вычисление размеров публичных ключей
    dilithium_param_t params1, params2, params3;
    dilithium_params_init(&params1, pub1->kind);
    dilithium_params_init(&params2, pub2->kind);
    dilithium_params_init(&params3, pub3->kind);

    printf("Key 1 public key size: %u bytes\n", params1.CRYPTO_PUBLICKEYBYTES);
    printf("Key 2 public key size: %u bytes\n", params2.CRYPTO_PUBLICKEYBYTES);
    printf("Key 3 public key size: %u bytes\n", params3.CRYPTO_PUBLICKEYBYTES);

    // Сравнение ключей с одинаковым seed
    if (params1.CRYPTO_PUBLICKEYBYTES == params2.CRYPTO_PUBLICKEYBYTES &&
        memcmp(pub1->data, pub2->data, params1.CRYPTO_PUBLICKEYBYTES) == 0) {
        printf("✅ Keys 1 and 2 are identical (same seed)\n");
    } else {
        printf("❌ Keys 1 and 2 are different\n");
    }

    // Сравнение ключей с разными seed
    if (params1.CRYPTO_PUBLICKEYBYTES == params3.CRYPTO_PUBLICKEYBYTES &&
        memcmp(pub1->data, pub3->data, params1.CRYPTO_PUBLICKEYBYTES) == 0) {
        printf("❌ Keys 1 and 3 are identical (different seeds should produce different keys)\n");
    } else {
        printf("✅ Keys 1 and 3 are different (different seeds)\n");
    }

    // Тестирование подписей
    const char *test_msg = "Deterministic key test message";
    size_t msg_len = strlen(test_msg);

    void *signature1 = malloc(sizeof(dilithium_signature_t));
    void *signature2 = malloc(sizeof(dilithium_signature_t));

    if (signature1 && signature2) {
        // Подпись ключом 1
        int sign1 = dap_enc_sig_dilithium_get_sign(key1, test_msg, msg_len,
                                                 signature1, sizeof(dilithium_signature_t));

        // Подпись ключом 2 (тот же seed)
        int sign2 = dap_enc_sig_dilithium_get_sign(key2, test_msg, msg_len,
                                                 signature2, sizeof(dilithium_signature_t));

        if (sign1 == 0 && sign2 == 0) {
            // Верификация подписи ключа 1 публичным ключом ключа 2
            int verify1 = dap_enc_sig_dilithium_verify_sign(key2, test_msg, msg_len,
                                                         signature1, sizeof(dilithium_signature_t));

            // Верификация подписи ключа 2 публичным ключом ключа 1
            int verify2 = dap_enc_sig_dilithium_verify_sign(key1, test_msg, msg_len,
                                                         signature2, sizeof(dilithium_signature_t));

            if (verify1 == 0 && verify2 == 0) {
                printf("✅ Cross-verification successful (same seed keys)\n");
            } else {
                printf("❌ Cross-verification failed\n");
            }
        }

        free(signature1);
        free(signature2);
    }

    printf("\n📊 Deterministic Key Generation:\n");
    printf("- Same seed produces identical key pairs\n");
    printf("- Different seeds produce different key pairs\n");
    printf("- Useful for reproducible key generation\n");
    printf("- Important for testing and backup scenarios\n");

    // Очистка
    dap_enc_sig_dilithium_key_delete(key1);
    dap_enc_sig_dilithium_key_delete(key2);
    dap_enc_sig_dilithium_key_delete(key3);

    DAP_DELETE(key1);
    DAP_DELETE(key2);
    DAP_DELETE(key3);

    return 0;
}
```

### Пример 4: Сериализация и хранение ключей

```c
#include "dap_enc_key.h"
#include "dap_enc_dilithium.h"
#include <stdio.h>

int dilithium_key_storage_example() {
    // Создание ключа
    dap_enc_sig_dilithium_set_type(DILITHIUM_MAX_SPEED);

    struct dap_enc_key *key = DAP_NEW(struct dap_enc_key);
    dap_enc_sig_dilithium_key_new(key);
    dap_enc_sig_dilithium_key_new_generate(key, NULL, 0, NULL, 0, 0);

    // Сериализация публичного ключа
    printf("Serializing Dilithium keys...\n");

    size_t pub_key_size;
    uint8_t *pub_key_data = dap_enc_sig_dilithium_write_public_key(
        key->pub_key_data, &pub_key_size);

    if (pub_key_data) {
        printf("Public key serialized: %zu bytes\n", pub_key_size);

        // Сохранение в файл
        FILE *pub_file = fopen("dilithium_public.key", "wb");
        if (pub_file) {
            fwrite(pub_key_data, 1, pub_key_size, pub_file);
            fclose(pub_file);
            printf("✅ Public key saved to file\n");
        }

        free(pub_key_data);
    }

    // Сериализация приватного ключа
    size_t priv_key_size;
    uint8_t *priv_key_data = dap_enc_sig_dilithium_write_private_key(
        key->priv_key_data, &priv_key_size);

    if (priv_key_data) {
        printf("Private key serialized: %zu bytes\n", priv_key_size);

        // Сохранение в файл (в реальном приложении используйте шифрование!)
        FILE *priv_file = fopen("dilithium_private.key", "wb");
        if (priv_file) {
            fwrite(priv_key_data, 1, priv_key_size, priv_file);
            fclose(priv_file);
            printf("✅ Private key saved to file\n");
            printf("⚠️  WARNING: Private key should be encrypted before storage!\n");
        }

        free(priv_key_data);
    }

    // Загрузка публичного ключа
    printf("\nLoading Dilithium public key...\n");

    FILE *load_pub_file = fopen("dilithium_public.key", "rb");
    if (load_pub_file) {
        fseek(load_pub_file, 0, SEEK_END);
        size_t file_size = ftell(load_pub_file);
        fseek(load_pub_file, 0, SEEK_SET);

        uint8_t *loaded_pub_data = malloc(file_size);
        if (fread(loaded_pub_data, 1, file_size, load_pub_file) == file_size) {
            fclose(load_pub_file);

            // Десериализация
            dilithium_public_key_t *loaded_pub_key = (dilithium_public_key_t *)
                dap_enc_sig_dilithium_read_public_key(loaded_pub_data, file_size);

            if (loaded_pub_key) {
                printf("✅ Public key loaded successfully\n");

                // Создание ключа для верификации
                struct dap_enc_key *verify_key = DAP_NEW(struct dap_enc_key);
                dap_enc_sig_dilithium_key_new(verify_key);
                verify_key->pub_key_data = loaded_pub_key;
                verify_key->pub_key_data_size = sizeof(dilithium_public_key_t);

                // Тестирование верификации
                const char *test_msg = "Test message for verification";
                size_t sig_size = sizeof(dilithium_signature_t);
                void *test_sig = malloc(sig_size);

                if (test_sig) {
                    int sign_result = dap_enc_sig_dilithium_get_sign(key, test_msg, strlen(test_msg),
                                                                   test_sig, sig_size);

                    if (sign_result == 0) {
                        int verify_result = dap_enc_sig_dilithium_verify_sign(verify_key,
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

                dap_enc_sig_dilithium_key_delete(verify_key);
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

    dap_enc_sig_dilithium_key_delete(key);
    DAP_DELETE(key);

    return 0;
}
```

### Пример 5: Производительность и метрики

```c
#include "dap_enc_key.h"
#include "dap_enc_dilithium.h"
#include <time.h>
#include <stdio.h>

#define PERFORMANCE_ITERATIONS 100

int dilithium_performance_metrics() {
    printf("Dilithium Performance Metrics\n");
    printf("=============================\n");

    // Настройка
    dap_enc_sig_dilithium_set_type(DILITHIUM_MAX_SPEED);

    // Создание ключа
    struct dap_enc_key *key = DAP_NEW(struct dap_enc_key);
    dap_enc_sig_dilithium_key_new(key);
    dap_enc_sig_dilithium_key_new_generate(key, NULL, 0, NULL, 0, 0);

    const char *test_data = "Performance testing data for Dilithium signatures";
    size_t data_size = strlen(test_data);

    printf("Test parameters:\n");
    printf("  Iterations: %d\n", PERFORMANCE_ITERATIONS);
    printf("  Data size: %zu bytes\n", data_size);

    // Получение информации о размерах
    dilithium_param_t params;
    dilithium_private_key_t *priv_key = (dilithium_private_key_t *)key->priv_key_data;
    dilithium_params_init(&params, priv_key->kind);

    printf("  Public key size: %u bytes\n", params.CRYPTO_PUBLICKEYBYTES);
    printf("  Private key size: %u bytes\n", params.CRYPTO_SECRETKEYBYTES);
    printf("  Signature size: %u bytes\n", params.CRYPTO_BYTES);

    // Тест генерации ключей
    printf("\n1. Key Generation Performance:\n");

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < PERFORMANCE_ITERATIONS; i++) {
        struct dap_enc_key *temp_key = DAP_NEW(struct dap_enc_key);
        dap_enc_sig_dilithium_key_new(temp_key);
        dap_enc_sig_dilithium_key_new_generate(temp_key, NULL, 0, NULL, 0, 0);
        dap_enc_sig_dilithium_key_delete(temp_key);
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
        void *signature = malloc(sizeof(dilithium_signature_t));
        if (signature) {
            dap_enc_sig_dilithium_get_sign(key, test_data, data_size,
                                         signature, sizeof(dilithium_signature_t));
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
    void *test_signature = malloc(sizeof(dilithium_signature_t));
    if (test_signature) {
        dap_enc_sig_dilithium_get_sign(key, test_data, data_size,
                                     test_signature, sizeof(dilithium_signature_t));

        clock_gettime(CLOCK_MONOTONIC, &start);

        for (int i = 0; i < PERFORMANCE_ITERATIONS; i++) {
            dap_enc_sig_dilithium_verify_sign(key, test_data, data_size,
                                            test_signature, sizeof(dilithium_signature_t));
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

    // Очистка
    dap_enc_sig_dilithium_key_delete(key);
    DAP_DELETE(key);

    return 0;
}
```

## Производительность

### Бенчмарки Dilithium

| Уровень | Операция | Производительность | Примечание |
|---------|----------|-------------------|------------|
| **Dilithium2** | Генерация ключей | ~100-200 μs | Intel Core i7 |
| **Dilithium2** | Создание подписи | ~50-80 μs | Intel Core i7 |
| **Dilithium2** | Верификация | ~30-50 μs | Intel Core i7 |
| **Dilithium3** | Генерация ключей | ~150-300 μs | Intel Core i7 |
| **Dilithium3** | Создание подписи | ~80-120 μs | Intel Core i7 |
| **Dilithium3** | Верификация | ~50-70 μs | Intel Core i7 |
| **Dilithium4** | Генерация ключей | ~200-400 μs | Intel Core i7 |
| **Dilithium4** | Создание подписи | ~120-180 μs | Intel Core i7 |
| **Dilithium4** | Верификация | ~70-100 μs | Intel Core i7 |

### Размеры ключей и подписей

| Уровень | Приватный ключ | Публичный ключ | Подпись | Безопасность |
|---------|----------------|----------------|---------|-------------|
| **Dilithium2** | 2,528 байт | 1,312 байт | 2,420 байт | 128-bit |
| **Dilithium3** | 4,000 байт | 1,952 байт | 3,293 байт | 160-bit |
| **Dilithium4** | 5,376 байт | 2,592 байт | 4,595 байт | 192-bit |

### Сравнение с другими алгоритмами

| Алгоритм | Безопасность | Подпись | Верификация | Размер подписи |
|----------|-------------|----------|-------------|----------------|
| **Dilithium2** | 128-bit PQ | ~65 μs | ~40 μs | 2,420 байт |
| **Falcon-512** | 128-bit PQ | ~250 μs | ~100 μs | 690 байт |
| **ECDSA P-256** | 128-bit | ~20 μs | ~40 μs | 64 байт |
| **Ed25519** | 128-bit | ~15 μs | ~30 μs | 64 байт |

## Безопасность

### Криптографическая стойкость

Dilithium обеспечивает:
- **128-bit безопасность** против классических атак (Dilithium2)
- **160-bit безопасность** против классических атак (Dilithium3)
- **192-bit безопасность** против классических атак (Dilithium4)
- **128-bit безопасность** против квантовых атак (все уровни)
- **EUF-CMA безопасность** (Existential Unforgeability under Chosen Message Attack)

### Рекомендации по использованию

#### Для новых проектов (рекомендуется):
```c
// Пост-квантовые подписи
dap_enc_sig_dilithium_set_type(DILITHIUM_MAX_SPEED); // Dilithium2 - 128-bit безопасность

struct dap_enc_key *key = DAP_NEW(struct dap_enc_key);
dap_enc_sig_dilithium_key_new(key);
dap_enc_sig_dilithium_key_new_generate(key, NULL, 0, NULL, 0, 0);
```

#### Для высокой безопасности:
```c
// Максимальная защита
dap_enc_sig_dilithium_set_type(DILITHIUM_MAX_SECURITY); // Dilithium4 - 192-bit безопасность
```

#### Для оптимизации размера:
```c
// Минимальный размер подписей
dap_enc_sig_dilithium_set_type(DILITHIUM_MIN_SIZE); // Dilithium3 - сбалансированный вариант
```

### Многоуровневая защита

```c
// Комбинация алгоритмов для максимальной безопасности
void create_multi_level_signature(const void *data, size_t data_size) {
    // Уровень 1: Dilithium (пост-квантовая защита)
    struct dap_enc_key *pq_key = create_dilithium_key(DILITHIUM_MAX_SECURITY);

    // Уровень 2: Falcon (дополнительная пост-квантовая защита)
    struct dap_enc_key *pq_key2 = create_falcon_key(FALCON_1024);

    // Уровень 3: ECDSA (классическая совместимость)
    struct dap_enc_key *classic_key = create_ecdsa_key();

    // Создание подписей всеми алгоритмами
    void *dilithium_sig = create_dilithium_signature(pq_key, data, data_size);
    void *falcon_sig = create_falcon_signature(pq_key2, data, data_size);
    void *ecdsa_sig = create_ecdsa_signature(classic_key, data, data_size);

    // Сохранение всех подписей для максимальной защиты
    // ...
}
```

## Лучшие практики

### 1. Выбор уровня безопасности

```c
// Правильный выбор уровня безопасности Dilithium
enum DAP_DILITHIUM_SIGN_SECURITY select_dilithium_level(
    bool high_security, bool high_speed, bool small_signatures) {

    if (high_security) {
        return DILITHIUM_MAX_SECURITY; // Dilithium4
    } else if (high_speed) {
        return DILITHIUM_MAX_SPEED;    // Dilithium2
    } else if (small_signatures) {
        return DILITHIUM_MIN_SIZE;     // Dilithium3
    } else {
        return DILITHIUM_MAX_SPEED;    // По умолчанию Dilithium2
    }
}
```

### 2. Управление жизненным циклом ключей

```c
// Безопасное управление ключами Dilithium
typedef struct dilithium_key_context {
    struct dap_enc_key *key;
    enum DAP_DILITHIUM_SIGN_SECURITY level;
    time_t created_time;
    uint32_t usage_count;
    bool compromised;
} dilithium_key_context_t;

dilithium_key_context_t *dilithium_context_create(enum DAP_DILITHIUM_SIGN_SECURITY level) {
    dilithium_key_context_t *ctx = calloc(1, sizeof(dilithium_key_context_t));
    if (!ctx) return NULL;

    // Настройка уровня безопасности
    dap_enc_sig_dilithium_set_type(level);

    // Создание ключа
    ctx->key = DAP_NEW(struct dap_enc_key);
    if (!ctx->key) {
        free(ctx);
        return NULL;
    }

    dap_enc_sig_dilithium_key_new(ctx->key);
    dap_enc_sig_dilithium_key_new_generate(ctx->key, NULL, 0, NULL, 0, 0);

    ctx->level = level;
    ctx->created_time = time(NULL);
    ctx->usage_count = 0;
    ctx->compromised = false;

    return ctx;
}

void dilithium_context_destroy(dilithium_key_context_t *ctx) {
    if (ctx) {
        if (ctx->key) {
            dap_enc_sig_dilithium_key_delete(ctx->key);
            DAP_DELETE(ctx->key);
        }
        free(ctx);
    }
}
```

### 3. Обработка ошибок

```c
// Надежная обработка ошибок Dilithium
int dilithium_secure_sign(const dilithium_key_context_t *ctx,
                         const void *data, size_t data_size,
                         void **signature, size_t *signature_size) {

    // Проверка параметров
    if (!ctx || !ctx->key || !data || data_size == 0 ||
        !signature || !signature_size) {
        return DILITHIUM_ERROR_INVALID_PARAMS;
    }

    // Проверка состояния ключа
    if (ctx->compromised) {
        return DILITHIUM_ERROR_KEY_COMPROMISED;
    }

    // Проверка размера данных
    if (data_size > DILITHIUM_MAX_MESSAGE_SIZE) {
        return DILITHIUM_ERROR_DATA_TOO_LARGE;
    }

    // Выделение памяти
    *signature = malloc(sizeof(dilithium_signature_t));
    if (!*signature) {
        return DILITHIUM_ERROR_MEMORY_ALLOCATION;
    }

    // Создание подписи
    int sign_result = dap_enc_sig_dilithium_get_sign(ctx->key, data, data_size,
                                                   *signature, sizeof(dilithium_signature_t));

    if (sign_result != 0) {
        free(*signature);
        *signature = NULL;
        return DILITHIUM_ERROR_SIGNING_FAILED;
    }

    *signature_size = sizeof(dilithium_signature_t);
    return DILITHIUM_SUCCESS;
}
```

### 4. Сериализация и хранение

```c
// Безопасная сериализация ключей Dilithium
int dilithium_secure_key_storage(const dilithium_key_context_t *ctx,
                                const char *public_key_file,
                                const char *private_key_file) {

    // Сериализация публичного ключа
    size_t pub_key_size;
    uint8_t *pub_key_data = dap_enc_sig_dilithium_write_public_key(
        ctx->key->pub_key_data, &pub_key_size);

    if (!pub_key_data) {
        return DILITHIUM_ERROR_SERIALIZATION_FAILED;
    }

    // Сохранение публичного ключа
    FILE *pub_file = fopen(public_key_file, "wb");
    if (!pub_file) {
        free(pub_key_data);
        return DILITHIUM_ERROR_FILE_ACCESS;
    }

    if (fwrite(pub_key_data, 1, pub_key_size, pub_file) != pub_key_size) {
        fclose(pub_file);
        free(pub_key_data);
        return DILITHIUM_ERROR_FILE_WRITE;
    }

    fclose(pub_file);
    free(pub_key_data);

    // Сериализация приватного ключа (с шифрованием!)
    size_t priv_key_size;
    uint8_t *priv_key_data = dap_enc_sig_dilithium_write_private_key(
        ctx->key->priv_key_data, &priv_key_size);

    if (!priv_key_data) {
        return DILITHIUM_ERROR_SERIALIZATION_FAILED;
    }

    // В реальном приложении зашифруйте priv_key_data перед сохранением!
    // uint8_t *encrypted_priv_key = aes_encrypt(priv_key_data, priv_key_size);

    FILE *priv_file = fopen(private_key_file, "wb");
    if (!priv_file) {
        free(priv_key_data);
        return DILITHIUM_ERROR_FILE_ACCESS;
    }

    if (fwrite(priv_key_data, 1, priv_key_size, priv_file) != priv_key_size) {
        fclose(priv_file);
        free(priv_key_data);
        return DILITHIUM_ERROR_FILE_WRITE;
    }

    fclose(priv_file);
    free(priv_key_data);

    return DILITHIUM_SUCCESS;
}
```

## Заключение

Модуль `dap_enc_dilithium` предоставляет высокопроизводительную реализацию пост-квантовых цифровых подписей Dilithium:

### Ключевые преимущества:
- **Пост-квантовая безопасность**: NIST finalist с доказанной безопасностью
- **Гибкие уровни безопасности**: От 128-bit до 192-bit
- **Оптимизированные реализации**: Разные варианты для разных сценариев использования
- **Стандартизованный**: Четко определенные параметры и уровни безопасности

### Основные возможности:
- Четыре уровня безопасности (2, 3, 4)
- Полная сериализация и десериализация
- Детерминированная генерация ключей
- Интеграция с системой ключей DAP SDK

### Рекомендации по использованию:
1. **Для большинства приложений**: Используйте Dilithium2 (MAX_SPEED)
2. **Для высокой безопасности**: Выбирайте Dilithium4 (MAX_SECURITY)
3. **Для оптимизации размера**: Используйте Dilithium3 (MIN_SIZE)
4. **Всегда проверяйте** результаты операций с подписями
5. **Безопасно храните** приватные ключи (с шифрованием)

### Следующие шаги:
1. Изучите другие пост-квантовые алгоритмы (Falcon, Sphincs+)
2. Ознакомьтесь с примерами использования
3. Интегрируйте Dilithium в свои приложения
4. Следите за развитием пост-квантовой криптографии

Для получения дополнительной информации смотрите:
- `dap_enc_dilithium.h` - полный API Dilithium
- `dilithium_params.h` - параметры и структуры данных
- Примеры в директории `examples/crypto/`
- Тесты в директории `test/crypto/`

