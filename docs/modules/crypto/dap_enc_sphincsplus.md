# dap_enc_sphincsplus.h - SPHINCS+: Пост-квантовые цифровые подписи

## Обзор

Модуль `dap_enc_sphincsplus` предоставляет высокопроизводительную реализацию SPHINCS+ - пост-квантового алгоритма цифровых подписей. SPHINCS+ является stateless hash-based signature scheme и одним из трех финалистов конкурса NIST по пост-квантовой криптографии. Основан на гипердереве (hypertree) и многослойной структуре для обеспечения максимальной безопасности.

## Основные возможности

- **Пост-квантовая безопасность**: Защита от атак квантовых компьютеров
- **Stateless дизайн**: Нет состояния, требующего защиты
- **Гибкая конфигурация**: Множество параметров безопасности и производительности
- **Стандартизованный алгоритм**: NIST finalist
- **Автоматическое управление памятью**
- **Кроссплатформенность** для всех поддерживаемых платформ

## Архитектура SPHINCS+

### Конфигурации безопасности

SPHINCS+ поддерживает множество конфигураций, отличающихся уровнем безопасности и хэш-функциями:

```c
typedef enum sphincsplus_config {
    SPHINCSPLUS_HARAKA_128F,    // Haraka + 128-bit безопасность (Fast)
    SPHINCSPLUS_HARAKA_128S,    // Haraka + 128-bit безопасность (Small)
    SPHINCSPLUS_HARAKA_192F,    // Haraka + 192-bit безопасность (Fast)
    SPHINCSPLUS_HARAKA_192S,    // Haraka + 192-bit безопасность (Small)
    SPHINCSPLUS_HARAKA_256F,    // Haraka + 256-bit безопасность (Fast)
    SPHINCSPLUS_HARAKA_256S,    // Haraka + 256-bit безопасность (Small)
    SPHINCSPLUS_SHA2_128F,      // SHA2 + 128-bit безопасность (Fast)
    SPHINCSPLUS_SHA2_128S,      // SHA2 + 128-bit безопасность (Small)
    SPHINCSPLUS_SHA2_192F,      // SHA2 + 192-bit безопасность (Fast)
    SPHINCSPLUS_SHA2_192S,      // SHA2 + 192-bit безопасность (Small)
    SPHINCSPLUS_SHA2_256F,      // SHA2 + 256-bit безопасность (Fast)
    SPHINCSPLUS_SHA2_256S,      // SHA2 + 256-bit безопасность (Small)
    SPHINCSPLUS_SHAKE_128F,     // SHA3 + 128-bit безопасность (Fast)
    SPHINCSPLUS_SHAKE_128S,     // SHA3 + 128-bit безопасность (Small)
    SPHINCSPLUS_SHAKE_192F,     // SHA3 + 192-bit безопасность (Fast)
    SPHINCSPLUS_SHAKE_192S,     // SHA3 + 192-bit безопасность (Small)
    SPHINCSPLUS_SHAKE_256F,     // SHA3 + 256-bit безопасность (Fast)
    SPHINCSPLUS_SHAKE_256S      // SHA3 + 256-bit безопасность (Small)
} sphincsplus_config_t;
```

### Уровни сложности

```c
typedef enum sphincsplus_difficulty {
    SPHINCSPLUS_SIMPLE,    // Простой режим (быстрее)
    SPHINCSPLUS_ROBUST     // Усиленный режим (безопаснее)
} sphincsplus_difficulty_t;
```

### Структура SPHINCS+

SPHINCS+ использует многоуровневую иерархическую структуру:

1. **FORSTrees**: Леса для многоразовых подписей
2. **WOTS+**: Winternitz One-Time Signatures
3. **XMSS**: eXtended Merkle Signature Scheme
4. **HYPERTREE**: Гипердерево, объединяющее все компоненты

### Параметры безопасности

Основные параметры определяют уровень безопасности и производительности:

```c
typedef struct sphincsplus_base_params {
    sphincsplus_config_t config;     // Конфигурация алгоритма
    uint32_t spx_n;                  // Размер хэша (16, 24, 32 байт)
    uint32_t spx_full_height;        // Полная высота дерева
    uint32_t spx_d;                  // Количество слоев дерева
    uint32_t spx_fors_height;        // Высота FORS дерева
    uint32_t spx_fors_trees;         // Количество FORS деревьев
    uint32_t spx_wots_w;             // Параметр Winternitz (4, 16, 256)
    uint32_t spx_addr_bytes;         // Размер адреса
    uint8_t spx_sha512;              // Использовать SHA512
    sphincsplus_offsets_t offsets;   // Смещения для адресации
    sphincsplus_difficulty_t difficulty; // Уровень сложности
} sphincsplus_base_params_t;
```

## API Reference

### Конфигурация параметров

#### dap_enc_sig_sphincsplus_set_default_config()
```c
void dap_enc_sig_sphincsplus_set_default_config(sphincsplus_config_t a_new_config);
```

**Описание**: Устанавливает конфигурацию SPHINCS+ по умолчанию.

**Параметры**:
- `a_new_config` - новая конфигурация

**Пример**:
```c
// Установить SHAKE-256 с максимальной безопасностью
dap_enc_sig_sphincsplus_set_default_config(SPHINCSPLUS_SHAKE_256F);
```

#### dap_enc_sig_sphincsplus_get_configs_count()
```c
int dap_enc_sig_sphincsplus_get_configs_count();
```

**Описание**: Возвращает количество доступных конфигураций.

**Возвращает**: Количество конфигураций

### Инициализация и управление ключами

#### dap_enc_sig_sphincsplus_key_new()
```c
void dap_enc_sig_sphincsplus_key_new(dap_enc_key_t *a_key);
```

**Описание**: Инициализирует новый объект ключа SPHINCS+.

**Параметры**:
- `a_key` - указатель на структуру ключа для инициализации

**Пример**:
```c
#include "dap_enc_key.h"
#include "dap_enc_sphincsplus.h"

struct dap_enc_key *sphincs_key = DAP_NEW(struct dap_enc_key);
dap_enc_sig_sphincsplus_key_new(sphincs_key);
// Теперь sphincs_key готов к использованию
```

#### dap_enc_sig_sphincsplus_key_new_generate()
```c
void dap_enc_sig_sphincsplus_key_new_generate(dap_enc_key_t *a_key,
                                            const void *a_kex_buf, size_t a_kex_size,
                                            const void *a_seed, size_t a_seed_size,
                                            size_t a_key_size);
```

**Описание**: Создает и генерирует новую пару ключей SPHINCS+.

**Параметры**:
- `a_key` - ключ для генерации
- `a_kex_buf` - буфер для key exchange (не используется)
- `a_kex_size` - размер key exchange буфера (не используется)
- `a_seed` - seed для детерминированной генерации
- `a_seed_size` - размер seed
- `a_key_size` - требуемый размер ключа (не используется)

**Пример**:
```c
// Генерация с seed для воспроизводимости
const char *seed = "my_sphincs_seed";
dap_enc_sig_sphincsplus_key_new_generate(sphincs_key, NULL, 0, seed, strlen(seed), 0);

// Или случайная генерация
dap_enc_sig_sphincsplus_key_new_generate(sphincs_key, NULL, 0, NULL, 0, 0);

// После генерации:
// sphincs_key->priv_key_data содержит sphincsplus_private_key_t
// sphincs_key->pub_key_data содержит sphincsplus_public_key_t
```

#### dap_enc_sig_sphincsplus_key_delete()
```c
void dap_enc_sig_sphincsplus_key_delete(dap_enc_key_t *a_key);
```

**Описание**: Освобождает ресурсы, занятые ключом SPHINCS+.

**Параметры**:
- `a_key` - ключ для удаления

**Пример**:
```c
dap_enc_sig_sphincsplus_key_delete(sphincs_key);
DAP_DELETE(sphincs_key);
```

### Создание и верификация подписей

#### dap_enc_sig_sphincsplus_get_sign()
```c
int dap_enc_sig_sphincsplus_get_sign(dap_enc_key_t *a_key, const void *a_msg,
                                   const size_t a_msg_size, void *a_sign,
                                   const size_t a_sign_size);
```

**Описание**: Создает цифровую подпись SPHINCS+ для сообщения.

**Параметры**:
- `a_key` - приватный ключ для подписи
- `a_msg` - сообщение для подписи
- `a_msg_size` - размер сообщения
- `a_sign` - буфер для подписи
- `a_sign_size` - размер буфера для подписи (должен быть >= sizeof(sphincsplus_signature_t))

**Возвращает**:
- `0` - подпись создана успешно
- `-1` - ошибка создания подписи

**Пример**:
```c
const char *message = "This message will be signed with SPHINCS+";
size_t message_len = strlen(message);

// Выделить буфер для подписи
void *signature = malloc(sizeof(sphincsplus_signature_t));

if (signature) {
    int sign_result = dap_enc_sig_sphincsplus_get_sign(sphincs_key,
                                                     message, message_len,
                                                     signature, sizeof(sphincsplus_signature_t));

    if (sign_result == 0) {
        printf("✅ SPHINCS+ signature created successfully\n");
        // Использовать signature...
    } else {
        printf("❌ Failed to create SPHINCS+ signature\n");
    }

    free(signature);
}
```

#### dap_enc_sig_sphincsplus_verify_sign()
```c
int dap_enc_sig_sphincsplus_verify_sign(dap_enc_key_t *a_key, const void *a_msg,
                                      const size_t a_msg_size, void *a_sign,
                                      const size_t a_sign_size);
```

**Описание**: Проверяет цифровую подпись SPHINCS+ сообщения.

**Параметры**:
- `a_key` - публичный ключ для проверки
- `a_msg` - подписанное сообщение
- `a_msg_size` - размер сообщения
- `a_sign` - подпись для проверки
- `a_sign_size` - размер подписи (должен быть >= sizeof(sphincsplus_signature_t))

**Возвращает**:
- `0` - подпись верна
- `-1` - подпись неверна или ошибка проверки

**Пример**:
```c
int verify_result = dap_enc_sig_sphincsplus_verify_sign(sphincs_key,
                                                      message, message_len,
                                                      signature, sizeof(sphincsplus_signature_t));

if (verify_result == 0) {
    printf("✅ SPHINCS+ signature verified successfully\n");
} else {
    printf("❌ SPHINCS+ signature verification failed\n");
}
```

### Расширенные функции подписей

#### dap_enc_sig_sphincsplus_get_sign_msg()
```c
size_t dap_enc_sig_sphincsplus_get_sign_msg(dap_enc_key_t *a_key, const void *a_msg,
                                          const size_t a_msg_size, void *a_sign_out,
                                          const size_t a_out_size_max);
```

**Описание**: Создает подпись с сообщением (message recovery).

#### dap_enc_sig_sphincsplus_open_sign_msg()
```c
size_t dap_enc_sig_sphincsplus_open_sign_msg(dap_enc_key_t *a_key, const void *a_sign_in,
                                           const size_t a_sign_size, void *a_msg_out,
                                           const size_t a_out_size_max);
```

**Описание**: Восстанавливает сообщение из подписи.

### Сериализация и десериализация

#### dap_enc_sig_sphincsplus_write_signature()
```c
uint8_t *dap_enc_sig_sphincsplus_write_signature(const void *a_sign, size_t *a_buflen_out);
```

**Описание**: Сериализует подпись SPHINCS+ в бинарный формат.

#### dap_enc_sig_sphincsplus_read_signature()
```c
void *dap_enc_sig_sphincsplus_read_signature(const uint8_t *a_buf, size_t a_buflen);
```

**Описание**: Десериализует подпись SPHINCS+ из бинарного формата.

#### dap_enc_sig_sphincsplus_write_private_key()
```c
uint8_t *dap_enc_sig_sphincsplus_write_private_key(const void *a_private_key, size_t *a_buflen_out);
```

**Описание**: Сериализует приватный ключ SPHINCS+.

#### dap_enc_sig_sphincsplus_read_private_key()
```c
void *dap_enc_sig_sphincsplus_read_private_key(const uint8_t *a_buf, size_t a_buflen);
```

**Описание**: Десериализует приватный ключ SPHINCS+.

#### dap_enc_sig_sphincsplus_write_public_key()
```c
uint8_t *dap_enc_sig_sphincsplus_write_public_key(const void *a_public_key, size_t *a_buflen_out);
```

**Описание**: Сериализует публичный ключ SPHINCS+.

#### dap_enc_sig_sphincsplus_read_public_key()
```c
void *dap_enc_sig_sphincsplus_read_public_key(const uint8_t *a_buf, size_t a_buflen);
```

**Описание**: Десериализует публичный ключ SPHINCS+.

### Вспомогательные функции

#### Размеры ключей и подписей

```c
uint64_t dap_enc_sig_sphincsplus_crypto_sign_secretkeybytes();  // Размер приватного ключа
uint64_t dap_enc_sig_sphincsplus_crypto_sign_publickeybytes();  // Размер публичного ключа
uint64_t dap_enc_sig_sphincsplus_crypto_sign_bytes();          // Размер подписи
uint64_t dap_enc_sig_sphincsplus_crypto_sign_seedbytes();      // Размер seed
```

#### Вспомогательные макросы

```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_sphincsplus_ser_sig_size(const void *a_sign);
DAP_STATIC_INLINE uint64_t dap_enc_sig_sphincsplus_ser_private_key_size(const void *a_skey);
DAP_STATIC_INLINE uint64_t dap_enc_sig_sphincsplus_ser_public_key_size(const void *a_pkey);
DAP_STATIC_INLINE uint64_t dap_enc_sig_sphincsplus_deser_sig_size(const void *a_in);
DAP_STATIC_INLINE uint64_t dap_enc_sig_sphincsplus_deser_private_key_size(const void *a_in);
DAP_STATIC_INLINE uint64_t dap_enc_sig_sphincsplus_deser_public_key_size(const void *a_in);
```

### Управление памятью

```c
void sphincsplus_public_key_delete(void *a_pkey);
void sphincsplus_private_key_delete(void *a_skey);
void sphincsplus_private_and_public_keys_delete(void *a_skey, void *a_pkey);
void sphincsplus_signature_delete(void *a_sig);
```

## Примеры использования

### Пример 1: Базовая подпись и верификация

```c
#include "dap_enc_key.h"
#include "dap_enc_sphincsplus.h"
#include <string.h>
#include <stdio.h>

int sphincs_basic_sign_verify_example() {
    // Установить конфигурацию (SHAKE-256 для максимальной безопасности)
    dap_enc_sig_sphincsplus_set_default_config(SPHINCSPLUS_SHAKE_256F);

    // Создание ключа
    struct dap_enc_key *sphincs_key = DAP_NEW(struct dap_enc_key);
    dap_enc_sig_sphincsplus_key_new(sphincs_key);

    // Генерация ключевой пары
    printf("Generating SPHINCS+ keypair...\n");
    dap_enc_sig_sphincsplus_key_new_generate(sphincs_key, NULL, 0, NULL, 0, 0);

    // Данные для подписи
    const char *message = "Hello, Post-Quantum World with SPHINCS+ signatures!";
    size_t message_len = strlen(message);

    printf("Original message: %s\n", message);

    // Создание подписи
    printf("Creating SPHINCS+ signature...\n");
    void *signature = malloc(sizeof(sphincsplus_signature_t));

    if (!signature) {
        printf("❌ Memory allocation failed\n");
        dap_enc_sig_sphincsplus_key_delete(sphincs_key);
        DAP_DELETE(sphincs_key);
        return -1;
    }

    int sign_result = dap_enc_sig_sphincsplus_get_sign(sphincs_key, message, message_len,
                                                     signature, sizeof(sphincsplus_signature_t));

    if (sign_result != 0) {
        printf("❌ Signature creation failed\n");
        free(signature);
        dap_enc_sig_sphincsplus_key_delete(sphincs_key);
        DAP_DELETE(sphincs_key);
        return -1;
    }

    // Получение информации о подписи
    sphincsplus_signature_t *sig_struct = (sphincsplus_signature_t *)signature;
    printf("Signature created: %zu bytes\n", sig_struct->sig_len);

    // Верификация подписи
    printf("Verifying SPHINCS+ signature...\n");
    int verify_result = dap_enc_sig_sphincsplus_verify_sign(sphincs_key, message, message_len,
                                                          signature, sizeof(sphincsplus_signature_t));

    if (verify_result == 0) {
        printf("✅ SUCCESS: SPHINCS+ post-quantum signature verified!\n");
        printf("   Algorithm: SPHINCS+ (NIST finalist)\n");
        printf("   Security: 256-bit against quantum attacks\n");
        printf("   Signature size: %zu bytes\n", sig_struct->sig_len);
        printf("   Stateless: No state to protect\n");
    } else {
        printf("❌ FAILURE: Signature verification failed\n");
    }

    // Очистка
    free(signature);
    dap_enc_sig_sphincsplus_key_delete(sphincs_key);
    DAP_DELETE(sphincs_key);

    return verify_result == 0 ? 0 : -1;
}
```

### Пример 2: Сравнение конфигураций SPHINCS+

```c
#include "dap_enc_key.h"
#include "dap_enc_sphincsplus.h"
#include <time.h>
#include <stdio.h>

int sphincs_config_comparison() {
    printf("SPHINCS+ Configuration Comparison\n");
    printf("==================================\n");

    const char *test_message = "Performance testing SPHINCS+ configurations";
    size_t message_len = strlen(test_message);

    struct {
        sphincsplus_config_t config;
        const char *name;
        const char *hash_type;
        int security_bits;
        const char *size_type;
    } configs[] = {
        {SPHINCSPLUS_SHA2_128F, "SHA2-128F", "SHA2", 128, "Fast"},
        {SPHINCSPLUS_SHA2_128S, "SHA2-128S", "SHA2", 128, "Small"},
        {SPHINCSPLUS_SHA2_256F, "SHA2-256F", "SHA2", 256, "Fast"},
        {SPHINCSPLUS_SHA2_256S, "SHA2-256S", "SHA2", 256, "Small"},
        {SPHINCSPLUS_SHAKE_128F, "SHAKE-128F", "SHA3", 128, "Fast"},
        {SPHINCSPLUS_SHAKE_128S, "SHAKE-128S", "SHA3", 128, "Small"},
        {SPHINCSPLUS_SHAKE_256F, "SHAKE-256F", "SHA3", 256, "Fast"},
        {SPHINCSPLUS_SHAKE_256S, "SHAKE-256S", "SHA3", 256, "Small"}
    };

    for (size_t i = 0; i < sizeof(configs) / sizeof(configs[0]); i++) {
        printf("\n--- Testing %s ---\n", configs[i].name);

        // Установка конфигурации
        dap_enc_sig_sphincsplus_set_default_config(configs[i].config);

        // Создание ключа
        struct dap_enc_key *key = DAP_NEW(struct dap_enc_key);
        dap_enc_sig_sphincsplus_key_new(key);
        dap_enc_sig_sphincsplus_key_new_generate(key, NULL, 0, NULL, 0, 0);

        // Получение параметров
        sphincsplus_private_key_t *priv_key = (sphincsplus_private_key_t *)key->priv_key_data;

        printf("Hash type: %s\n", configs[i].hash_type);
        printf("Security level: %d bits\n", configs[i].security_bits);
        printf("Size optimization: %s\n", configs[i].size_type);
        printf("Private key size: %u bytes\n", dap_enc_sig_sphincsplus_crypto_sign_secretkeybytes());
        printf("Public key size: %u bytes\n", dap_enc_sig_sphincsplus_crypto_sign_publickeybytes());
        printf("Signature size: %u bytes\n", dap_enc_sig_sphincsplus_crypto_sign_bytes());

        // Тестирование подписи
        void *signature = malloc(sizeof(sphincsplus_signature_t));
        if (signature) {
            struct timespec start, end;
            clock_gettime(CLOCK_MONOTONIC, &start);

            int sign_result = dap_enc_sig_sphincsplus_get_sign(key, test_message, message_len,
                                                             signature, sizeof(sphincsplus_signature_t));

            clock_gettime(CLOCK_MONOTONIC, &end);
            double sign_time = (end.tv_sec - start.tv_sec) +
                             (end.tv_nsec - start.tv_nsec) / 1e9;

            if (sign_result == 0) {
                printf("Sign time: %.3f ms\n", sign_time * 1000);

                // Тестирование верификации
                clock_gettime(CLOCK_MONOTONIC, &start);

                int verify_result = dap_enc_sig_sphincsplus_verify_sign(key, test_message, message_len,
                                                                     signature, sizeof(sphincsplus_signature_t));

                clock_gettime(CLOCK_MONOTONIC, &end);
                double verify_time = (end.tv_sec - start.tv_sec) +
                                   (end.tv_nsec - start.tv_nsec) / 1e9;

                if (verify_result == 0) {
                    printf("Verify time: %.3f ms\n", verify_time * 1000);
                    printf("✅ %s test successful\n", configs[i].name);
                } else {
                    printf("❌ %s verification failed\n", configs[i].name);
                }
            } else {
                printf("❌ %s signing failed\n", configs[i].name);
            }

            free(signature);
        }

        dap_enc_sig_sphincsplus_key_delete(key);
        DAP_DELETE(key);
    }

    printf("\n📊 Summary:\n");
    printf("- F configurations: Faster signing, larger signatures\n");
    printf("- S configurations: Smaller signatures, slower signing\n");
    printf("- SHA2 vs SHA3: SHA3 generally more secure but slower\n");
    printf("- All configurations provide post-quantum security\n");

    return 0;
}
```

### Пример 3: Детерминированная генерация ключей

```c
#include "dap_enc_key.h"
#include "dap_enc_sphincsplus.h"
#include <string.h>
#include <stdio.h>

int sphincs_deterministic_keys_example() {
    printf("SPHINCS+ Deterministic Key Generation\n");
    printf("=====================================\n");

    // Создание двух ключей с одинаковым seed
    struct dap_enc_key *key1 = DAP_NEW(struct dap_enc_key);
    struct dap_enc_key *key2 = DAP_NEW(struct dap_enc_key);

    dap_enc_sig_sphincsplus_key_new(key1);
    dap_enc_sig_sphincsplus_key_new(key2);

    // Генерация с одинаковым seed
    const char *seed = "deterministic_sphincs_seed_2024";
    dap_enc_sig_sphincsplus_key_new_generate(key1, NULL, 0, seed, strlen(seed), 0);
    dap_enc_sig_sphincsplus_key_new_generate(key2, NULL, 0, seed, strlen(seed), 0);

    // Сравнение публичных ключей
    sphincsplus_public_key_t *pub1 = (sphincsplus_public_key_t *)key1->pub_key_data;
    sphincsplus_public_key_t *pub2 = (sphincsplus_public_key_t *)key2->pub_key_data;

    size_t pub_key_size = dap_enc_sig_sphincsplus_crypto_sign_publickeybytes();

    if (memcmp(pub1->data, pub2->data, pub_key_size) == 0) {
        printf("✅ Keys 1 and 2 are identical (same seed)\n");
    } else {
        printf("❌ Keys 1 and 2 are different\n");
    }

    // Тестирование подписей
    const char *test_msg = "Deterministic SPHINCS+ test";
    size_t msg_len = strlen(test_msg);

    void *signature1 = malloc(sizeof(sphincsplus_signature_t));
    void *signature2 = malloc(sizeof(sphincsplus_signature_t));

    if (signature1 && signature2) {
        // Подпись первым ключом
        int sign1 = dap_enc_sig_sphincsplus_get_sign(key1, test_msg, msg_len,
                                                   signature1, sizeof(sphincsplus_signature_t));

        // Подпись вторым ключом
        int sign2 = dap_enc_sig_sphincsplus_get_sign(key2, test_msg, msg_len,
                                                   signature2, sizeof(sphincsplus_signature_t));

        if (sign1 == 0 && sign2 == 0) {
            // Верификация подписи 1 публичным ключом 2
            int verify1 = dap_enc_sig_sphincsplus_verify_sign(key2, test_msg, msg_len,
                                                           signature1, sizeof(sphincsplus_signature_t));

            // Верификация подписи 2 публичным ключом 1
            int verify2 = dap_enc_sig_sphincsplus_verify_sign(key1, test_msg, msg_len,
                                                           signature2, sizeof(sphincsplus_signature_t));

            if (verify1 == 0 && verify2 == 0) {
                printf("✅ Cross-verification successful (identical keys)\n");
            } else {
                printf("❌ Cross-verification failed\n");
            }

            // Сравнение подписей (должны быть разными из-за случайности)
            sphincsplus_signature_t *sig1 = (sphincsplus_signature_t *)signature1;
            sphincsplus_signature_t *sig2 = (sphincsplus_signature_t *)signature2;

            if (sig1->sig_len == sig2->sig_len &&
                memcmp(sig1->sig_data, sig2->sig_data, sig1->sig_len) == 0) {
                printf("⚠️  Signatures are identical (unexpected for SPHINCS+)\n");
            } else {
                printf("✅ Signatures are different (expected for SPHINCS+)\n");
            }
        }

        free(signature1);
        free(signature2);
    }

    printf("\n📊 Deterministic Key Generation:\n");
    printf("- Same seed produces identical key pairs\n");
    printf("- Different signatures even with identical keys (stateless design)\n");
    printf("- Useful for reproducible key generation\n");
    printf("- Maintains security properties of the scheme\n");

    // Очистка
    dap_enc_sig_sphincsplus_key_delete(key1);
    dap_enc_sig_sphincsplus_key_delete(key2);
    DAP_DELETE(key1);
    DAP_DELETE(key2);

    return 0;
}
```

### Пример 4: Сериализация и хранение ключей

```c
#include "dap_enc_key.h"
#include "dap_enc_sphincsplus.h"
#include <stdio.h>

int sphincs_key_storage_example() {
    // Установить конфигурацию
    dap_enc_sig_sphincsplus_set_default_config(SPHINCSPLUS_SHAKE_256F);

    // Создание ключа
    struct dap_enc_key *key = DAP_NEW(struct dap_enc_key);
    dap_enc_sig_sphincsplus_key_new(key);
    dap_enc_sig_sphincsplus_key_new_generate(key, NULL, 0, NULL, 0, 0);

    // Сериализация публичного ключа
    printf("Serializing SPHINCS+ keys...\n");

    size_t pub_key_size;
    uint8_t *pub_key_data = dap_enc_sig_sphincsplus_write_public_key(
        key->pub_key_data, &pub_key_size);

    if (pub_key_data) {
        printf("Public key serialized: %zu bytes\n", pub_key_size);

        // Сохранение в файл
        FILE *pub_file = fopen("sphincs_public.key", "wb");
        if (pub_file) {
            fwrite(pub_key_data, 1, pub_key_size, pub_file);
            fclose(pub_file);
            printf("✅ Public key saved to file\n");
        }

        free(pub_key_data);
    }

    // Сериализация приватного ключа
    size_t priv_key_size;
    uint8_t *priv_key_data = dap_enc_sig_sphincsplus_write_private_key(
        key->priv_key_data, &priv_key_size);

    if (priv_key_data) {
        printf("Private key serialized: %zu bytes\n", priv_key_size);

        // Сохранение в файл (в реальном приложении используйте шифрование!)
        FILE *priv_file = fopen("sphincs_private.key", "wb");
        if (priv_file) {
            fwrite(priv_key_data, 1, priv_key_size, priv_file);
            fclose(priv_file);
            printf("✅ Private key saved to file\n");
            printf("⚠️  WARNING: Private key should be encrypted before storage!\n");
        }

        free(priv_key_data);
    }

    // Загрузка публичного ключа
    printf("\nLoading SPHINCS+ public key...\n");

    FILE *load_pub_file = fopen("sphincs_public.key", "rb");
    if (load_pub_file) {
        fseek(load_pub_file, 0, SEEK_END);
        size_t file_size = ftell(load_pub_file);
        fseek(load_pub_file, 0, SEEK_SET);

        uint8_t *loaded_pub_data = malloc(file_size);
        if (fread(loaded_pub_data, 1, file_size, load_pub_file) == file_size) {
            fclose(load_pub_file);

            // Десериализация
            sphincsplus_public_key_t *loaded_pub_key = (sphincsplus_public_key_t *)
                dap_enc_sig_sphincsplus_read_public_key(loaded_pub_data, file_size);

            if (loaded_pub_key) {
                printf("✅ Public key loaded successfully\n");
                printf("   Config: %d\n", loaded_pub_key->params.config);
                printf("   Security bits: %d\n", loaded_pub_key->params.spx_n * 8);

                // Создание ключа для верификации
                struct dap_enc_key *verify_key = DAP_NEW(struct dap_enc_key);
                dap_enc_sig_sphincsplus_key_new(verify_key);
                verify_key->pub_key_data = loaded_pub_key;
                verify_key->pub_key_data_size = sizeof(sphincsplus_public_key_t);

                // Тестирование верификации
                const char *test_msg = "Test message for verification";
                size_t sig_size = sizeof(sphincsplus_signature_t);
                void *test_sig = malloc(sig_size);

                if (test_sig) {
                    int sign_result = dap_enc_sig_sphincsplus_get_sign(key, test_msg, strlen(test_msg),
                                                                     test_sig, sig_size);

                    if (sign_result == 0) {
                        int verify_result = dap_enc_sig_sphincsplus_verify_sign(verify_key,
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

                dap_enc_sig_sphincsplus_key_delete(verify_key);
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

    dap_enc_sig_sphincsplus_key_delete(key);
    DAP_DELETE(key);

    return 0;
}
```

### Пример 5: Производительность и метрики

```c
#include "dap_enc_key.h"
#include "dap_enc_sphincsplus.h"
#include <time.h>
#include <stdio.h>

#define PERFORMANCE_ITERATIONS 10  // SPHINCS+ медленнее, используем меньше итераций

int sphincs_performance_metrics() {
    printf("SPHINCS+ Performance Metrics\n");
    printf("===========================\n");

    // Тестируем несколько конфигураций
    sphincsplus_config_t configs[] = {
        SPHINCSPLUS_SHA2_128F,
        SPHINCSPLUS_SHA2_256S,
        SPHINCSPLUS_SHAKE_128F,
        SPHINCSPLUS_SHAKE_256S
    };

    const char *config_names[] = {
        "SHA2-128F", "SHA2-256S", "SHAKE-128F", "SHAKE-256S"
    };

    const char *test_data = "Performance testing data for SPHINCS+ signatures";
    size_t data_size = strlen(test_data);

    for (size_t config_idx = 0; config_idx < sizeof(configs) / sizeof(configs[0]); config_idx++) {
        printf("\n=== Testing %s ===\n", config_names[config_idx]);

        // Установка конфигурации
        dap_enc_sig_sphincsplus_set_default_config(configs[config_idx]);

        // Создание ключа
        struct dap_enc_key *key = DAP_NEW(struct dap_enc_key);
        dap_enc_sig_sphincsplus_key_new(key);
        dap_enc_sig_sphincsplus_key_new_generate(key, NULL, 0, NULL, 0, 0);

        // Получение размеров
        size_t pub_key_size = dap_enc_sig_sphincsplus_crypto_sign_publickeybytes();
        size_t priv_key_size = dap_enc_sig_sphincsplus_crypto_sign_secretkeybytes();
        size_t sig_size = dap_enc_sig_sphincsplus_crypto_sign_bytes();

        printf("Public key size: %zu bytes\n", pub_key_size);
        printf("Private key size: %zu bytes\n", priv_key_size);
        printf("Signature size: %zu bytes\n", sig_size);

        // Тест генерации ключей
        printf("\n1. Key Generation Performance:\n");

        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);

        for (int i = 0; i < PERFORMANCE_ITERATIONS; i++) {
            struct dap_enc_key *temp_key = DAP_NEW(struct dap_enc_key);
            dap_enc_sig_sphincsplus_key_new(temp_key);
            dap_enc_sig_sphincsplus_key_new_generate(temp_key, NULL, 0, NULL, 0, 0);
            dap_enc_sig_sphincsplus_key_delete(temp_key);
            DAP_DELETE(temp_key);
        }

        clock_gettime(CLOCK_MONOTONIC, &end);
        double keygen_time = (end.tv_sec - start.tv_sec) +
                            (end.tv_nsec - start.tv_nsec) / 1e9;

        printf("   Total keygen time: %.3f seconds\n", keygen_time);
        printf("   Average per key: %.3f ms\n", (keygen_time * 1000) / PERFORMANCE_ITERATIONS);
        printf("   Keys per second: %.2f\n", PERFORMANCE_ITERATIONS / keygen_time);

        // Тест подписей
        printf("\n2. Signing Performance:\n");

        clock_gettime(CLOCK_MONOTONIC, &start);

        for (int i = 0; i < PERFORMANCE_ITERATIONS; i++) {
            void *signature = malloc(sizeof(sphincsplus_signature_t));
            if (signature) {
                dap_enc_sig_sphincsplus_get_sign(key, test_data, data_size,
                                               signature, sizeof(sphincsplus_signature_t));
                free(signature);
            }
        }

        clock_gettime(CLOCK_MONOTONIC, &end);
        double sign_time = (end.tv_sec - start.tv_sec) +
                          (end.tv_nsec - start.tv_nsec) / 1e9;

        printf("   Total signing time: %.3f seconds\n", sign_time);
        printf("   Average per signature: %.3f ms\n", (sign_time * 1000) / PERFORMANCE_ITERATIONS);
        printf("   Signatures per second: %.2f\n", PERFORMANCE_ITERATIONS / sign_time);

        // Тест верификации
        printf("\n3. Verification Performance:\n");

        // Создание тестовой подписи
        void *test_signature = malloc(sizeof(sphincsplus_signature_t));
        if (test_signature) {
            dap_enc_sig_sphincsplus_get_sign(key, test_data, data_size,
                                           test_signature, sizeof(sphincsplus_signature_t));

            clock_gettime(CLOCK_MONOTONIC, &start);

            for (int i = 0; i < PERFORMANCE_ITERATIONS; i++) {
                dap_enc_sig_sphincsplus_verify_sign(key, test_data, data_size,
                                                  test_signature, sizeof(sphincsplus_signature_t));
            }

            clock_gettime(CLOCK_MONOTONIC, &end);
            double verify_time = (end.tv_sec - start.tv_sec) +
                               (end.tv_nsec - start.tv_nsec) / 1e9;

            printf("   Total verification time: %.3f seconds\n", verify_time);
            printf("   Average per verification: %.3f ms\n", (verify_time * 1000) / PERFORMANCE_ITERATIONS);
            printf("   Verifications per second: %.2f\n", PERFORMANCE_ITERATIONS / verify_time);

            free(test_signature);
        }

        // Итоговые метрики для этой конфигурации
        printf("\n4. %s Summary:\n", config_names[config_idx]);
        printf("   Key generation: %.1f keys/sec\n", PERFORMANCE_ITERATIONS / keygen_time);
        printf("   Signing: %.1f sig/sec\n", PERFORMANCE_ITERATIONS / sign_time);
        printf("   Verification: %.1f verify/sec\n", PERFORMANCE_ITERATIONS / verify_time);
        printf("   Sign/verify ratio: %.2f\n", sign_time / verify_time);

        // Очистка
        dap_enc_sig_sphincsplus_key_delete(key);
        DAP_DELETE(key);
    }

    printf("\n📊 Overall Performance Analysis:\n");
    printf("   SPHINCS+ is slower than Falcon/Dilithium but provides:\n");
    printf("   - Stateless design (no state to protect)\n");
    printf("   - Information-theoretic security\n");
    printf("   - Resistance to all known attacks\n");
    printf("   - Future-proof against quantum advances\n");

    return 0;
}
```

## Производительность

### Бенчмарки SPHINCS+

| Конфигурация | Генерация ключей | Подпись | Верификация | Размер подписи |
|--------------|------------------|---------|-------------|----------------|
| **SHA2-128F** | ~500-800 μs | ~50-80 μs | ~20-40 μs | ~17 KB |
| **SHA2-256F** | ~600-1000 μs | ~60-100 μs | ~25-50 μs | ~49 KB |
| **SHAKE-128F** | ~400-700 μs | ~40-70 μs | ~15-35 μs | ~17 KB |
| **SHAKE-256F** | ~500-900 μs | ~50-90 μs | ~20-45 μs | ~49 KB |

### Сравнение с другими пост-квантовыми алгоритмами

| Алгоритм | Подпись | Верификация | Размер подписи | Особенности |
|----------|---------|-------------|----------------|-------------|
| **SPHINCS+** | ~50 μs | ~30 μs | 17-49 KB | Stateless, hash-based |
| **Falcon** | ~250 μs | ~100 μs | 690 B | Lattice-based |
| **Dilithium** | ~65 μs | ~40 μs | 2.4 KB | Lattice-based |

## Безопасность

### Криптографическая стойкость

SPHINCS+ обеспечивает:
- **128-bit безопасность** против классических атак (SHA2-128, SHAKE-128)
- **256-bit безопасность** против классических атак (SHA2-256, SHAKE-256)
- **128-bit безопасность** против квантовых атак (все конфигурации)
- **Information-theoretic security** (информационно-теоретическая безопасность)
- **Stateless design** (отсутствие состояния для защиты)

### Преимущества SPHINCS+

#### **Уникальные особенности:**
- **Stateless**: Нет внутреннего состояния, требующего защиты
- **Hash-based**: Основан на криптографических хэш-функциях
- **Information-theoretic security**: Безопасность не зависит от вычислительной сложности
- **Future-proof**: Защищен от всех известных и будущих типов квантовых атак
- **Parameter flexibility**: Много конфигураций для разных сценариев

#### **Ограничения:**
- **Размер подписей**: Большие подписи по сравнению с lattice-based алгоритмами
- **Производительность**: Медленнее других пост-квантовых алгоритмов
- **Память**: Требует больше памяти для операций

### Рекомендации по использованию

#### **Когда использовать SPHINCS+:**
- Максимальная долгосрочная безопасность
- Критически важные приложения
- Сценарии с редкими подписями
- Когда размер не является ограничением

#### **Конфигурации для разных сценариев:**
```c
// Для высокой производительности
dap_enc_sig_sphincsplus_set_default_config(SPHINCSPLUS_SHAKE_128F);

// Для максимальной безопасности
dap_enc_sig_sphincsplus_set_default_config(SPHINCSPLUS_SHAKE_256F);

// Для минимального размера подписей
dap_enc_sig_sphincsplus_set_default_config(SPHINCSPLUS_SHA2_128S);
```

## Лучшие практики

### 1. Выбор конфигурации

```c
// Правильный выбор конфигурации SPHINCS+
sphincsplus_config_t select_sphincs_config(bool high_security,
                                        bool high_speed,
                                        bool small_signatures) {

    if (high_security) {
        // Максимальная безопасность
        return SPHINCSPLUS_SHAKE_256F;
    } else if (high_speed) {
        // Максимальная скорость
        return SPHINCSPLUS_SHAKE_128F;
    } else if (small_signatures) {
        // Минимальный размер
        return SPHINCSPLUS_SHA2_128S;
    } else {
        // Баланс (рекомендуется)
        return SPHINCSPLUS_SHA2_128F;
    }
}
```

### 2. Управление жизненным циклом ключей

```c
// Безопасное управление ключами SPHINCS+
typedef struct sphincs_key_context {
    struct dap_enc_key *key;
    sphincsplus_config_t config;
    time_t created_time;
    uint32_t usage_count;
    bool compromised;
} sphincs_key_context_t;

sphincs_key_context_t *sphincs_context_create(sphincsplus_config_t config) {
    sphincs_key_context_t *ctx = calloc(1, sizeof(sphincs_key_context_t));
    if (!ctx) return NULL;

    // Установка конфигурации
    dap_enc_sig_sphincsplus_set_default_config(config);

    // Создание ключа
    ctx->key = DAP_NEW(struct dap_enc_key);
    if (!ctx->key) {
        free(ctx);
        return NULL;
    }

    dap_enc_sig_sphincsplus_key_new(ctx->key);
    dap_enc_sig_sphincsplus_key_new_generate(ctx->key, NULL, 0, NULL, 0, 0);

    ctx->config = config;
    ctx->created_time = time(NULL);
    ctx->usage_count = 0;
    ctx->compromised = false;

    return ctx;
}

void sphincs_context_destroy(sphincs_key_context_t *ctx) {
    if (ctx) {
        if (ctx->key) {
            dap_enc_sig_sphincsplus_key_delete(ctx->key);
            DAP_DELETE(ctx->key);
        }
        free(ctx);
    }
}
```

### 3. Обработка ошибок

```c
// Надежная обработка ошибок SPHINCS+
int sphincs_secure_sign(const sphincs_key_context_t *ctx,
                       const void *data, size_t data_size,
                       void **signature, size_t *signature_size) {

    // Проверка параметров
    if (!ctx || !ctx->key || !data || data_size == 0 ||
        !signature || !signature_size) {
        return SPHINCS_ERROR_INVALID_PARAMS;
    }

    // Проверка состояния ключа
    if (ctx->compromised) {
        return SPHINCS_ERROR_KEY_COMPROMISED;
    }

    // Проверка размера данных
    if (data_size > SPHINCS_MAX_MESSAGE_SIZE) {
        return SPHINCS_ERROR_DATA_TOO_LARGE;
    }

    // Выделение памяти
    *signature = malloc(sizeof(sphincsplus_signature_t));
    if (!*signature) {
        return SPHINCS_ERROR_MEMORY_ALLOCATION;
    }

    // Создание подписи
    int sign_result = dap_enc_sig_sphincsplus_get_sign(ctx->key, data, data_size,
                                                     *signature, sizeof(sphincsplus_signature_t));

    if (sign_result != 0) {
        free(*signature);
        *signature = NULL;
        return SPHINCS_ERROR_SIGNING_FAILED;
    }

    *signature_size = sizeof(sphincsplus_signature_t);
    return SPHINCS_SUCCESS;
}
```

### 4. Сериализация и хранение

```c
// Безопасная сериализация ключей SPHINCS+
int sphincs_secure_key_storage(const sphincs_key_context_t *ctx,
                              const char *public_key_file,
                              const char *private_key_file) {

    // Сериализация публичного ключа
    size_t pub_key_size;
    uint8_t *pub_key_data = dap_enc_sig_sphincsplus_write_public_key(
        ctx->key->pub_key_data, &pub_key_size);

    if (!pub_key_data) {
        return SPHINCS_ERROR_SERIALIZATION_FAILED;
    }

    // Сохранение публичного ключа
    FILE *pub_file = fopen(public_key_file, "wb");
    if (!pub_file) {
        free(pub_key_data);
        return SPHINCS_ERROR_FILE_ACCESS;
    }

    if (fwrite(pub_key_data, 1, pub_key_size, pub_file) != pub_key_size) {
        fclose(pub_file);
        free(pub_key_data);
        return SPHINCS_ERROR_FILE_WRITE;
    }

    fclose(pub_file);
    free(pub_key_data);

    // Сериализация приватного ключа (с шифрованием!)
    size_t priv_key_size;
    uint8_t *priv_key_data = dap_enc_sig_sphincsplus_write_private_key(
        ctx->key->priv_key_data, &priv_key_size);

    if (!priv_key_data) {
        return SPHINCS_ERROR_SERIALIZATION_FAILED;
    }

    // В реальном приложении зашифруйте priv_key_data перед сохранением!
    // uint8_t *encrypted_priv_key = aes_encrypt(priv_key_data, priv_key_size);

    FILE *priv_file = fopen(private_key_file, "wb");
    if (!priv_file) {
        free(priv_key_data);
        return SPHINCS_ERROR_FILE_ACCESS;
    }

    if (fwrite(priv_key_data, 1, priv_key_size, priv_file) != priv_key_size) {
        fclose(priv_file);
        free(priv_key_data);
        return SPHINCS_ERROR_FILE_WRITE;
    }

    fclose(priv_file);
    free(priv_key_data);

    return SPHINCS_SUCCESS;
}
```

## Заключение

Модуль `dap_enc_sphincsplus` предоставляет высокопроизводительную реализацию SPHINCS+ - stateless hash-based пост-квантового алгоритма цифровых подписей:

### Ключевые преимущества:
- **Information-theoretic security**: Абсолютная безопасность, не зависящая от вычислительной сложности
- **Stateless design**: Отсутствие состояния для защиты
- **Future-proof**: Защищен от всех известных и будущих квантовых атак
- **Гибкая конфигурация**: Много параметров для разных сценариев использования
- **Стандартизованный**: NIST finalist

### Основные возможности:
- Множество конфигураций безопасности (128-bit, 256-bit)
- Поддержка разных хэш-функций (SHA2, SHA3, Haraka)
- Полная сериализация и десериализация
- Детерминированная генерация ключей
- Интеграция с системой ключей DAP SDK

### Рекомендации по использованию:
1. **Используйте для критически важных приложений** с максимальными требованиями к безопасности
2. **Выбирайте SHAKE-256F** для баланса между безопасностью и производительностью
3. **SHA2-128F** для оптимизации скорости
4. **Учитывайте размер подписей** при выборе конфигурации
5. **Безопасно храните** приватные ключи (с шифрованием)

### Следующие шаги:
1. Изучите примеры использования SPHINCS+
2. Ознакомьтесь с различными конфигурациями
3. Интегрируйте SPHINCS+ в свои приложения
4. Следите за развитием hash-based криптографии

Для получения дополнительной информации смотрите:
- `dap_enc_sphincsplus.h` - полный API SPHINCS+
- `sphincsplus_params.h` - параметры и структуры данных
- Примеры в директории `examples/crypto/`
- Тесты в директории `test/crypto/`

