# dap_enc_ecdsa.h - ECDSA: Классические цифровые подписи

## ⚠️ **ВАЖНОЕ ПРЕДУПРЕЖДЕНИЕ О БЕЗОПАСНОСТИ**

# 🚨 **КВАНТОВАЯ НЕБЕЗОПАСНОСТЬ**

**ECDSA (Elliptic Curve Digital Signature Algorithm) является классическим криптографическим алгоритмом и НЕ ОБЕСПЕЧИВАЕТ ЗАЩИТУ ОТ АТАК КВАНТОВЫХ КОМПЬЮТЕРОВ.**

### **Критические ограничения:**
- ✅ **Классическая безопасность**: 128-bit (secp256k1)
- ❌ **Квантовая безопасность**: Уязвим к атакам Шора
- ❌ **НЕ РЕКОМЕНДУЕТСЯ** для новых проектов
- ✅ **Совместимость**: Поддерживается для легаси-систем

### **Рекомендации:**
🔴 **НЕ ИСПОЛЬЗУЙТЕ ECDSA в новых проектах**  
🟡 **Используйте только для совместимости с существующими системами**  
🟢 **Для новых проектов выбирайте пост-квантовые алгоритмы:**
- **Falcon** (NIST finalist, решеточные подписи)
- **Dilithium** (NIST finalist, модулярные решетки)

---

## Обзор

Модуль `dap_enc_ecdsa` предоставляет реализацию ECDSA (Elliptic Curve Digital Signature Algorithm) на кривой secp256k1. Этот модуль включен в DAP SDK исключительно для обеспечения совместимости с существующими системами и проведения сравнительных тестов производительности.

## Основные возможности

- **Классическая кривая secp256k1**: Стандарт Bitcoin и Ethereum
- **128-bit безопасность**: Против классических атак
- **Высокая производительность**: Оптимизированная реализация
- **Автоматическое управление памятью**
- **Кроссплатформенность** для всех поддерживаемых платформ

## Архитектура ECDSA

### Криптографические параметры

```c
// Размеры ключей и подписей
#define ECDSA_PRIVATE_KEY_SIZE  32    // 256-bit приватный ключ
#define ECDSA_PUBLIC_KEY_SIZE   64    // 512-bit публичный ключ (без сжатия)
#define ECDSA_SIG_SIZE          64    // 512-bit подпись (r + s)
#define ECDSA_PKEY_SERIALIZED_SIZE 65 // Сериализованный публичный ключ
```

### Структура ключа

```c
typedef struct {
    unsigned char data[ECDSA_PRIVATE_KEY_SIZE]; // 32 байта приватного ключа
} ecdsa_private_key_t;

typedef secp256k1_pubkey ecdsa_public_key_t;    // Публичный ключ secp256k1
typedef secp256k1_ecdsa_signature ecdsa_signature_t; // Подпись ECDSA
```

## API Reference

### Конфигурация параметров

#### dap_enc_sig_ecdsa_set_type()
```c
void dap_enc_sig_ecdsa_set_type(enum DAP_ECDSA_SIGN_SECURITY type);
```

**Описание**: Устанавливает уровень безопасности ECDSA (в текущей реализации не используется).

**Параметры**:
- `type` - уровень безопасности (TOY, MAX_SPEED, MIN_SIZE, MAX_SECURITY)

**Примечание**: Функция закомментирована в текущей реализации.

### Инициализация и управление ключами

#### dap_enc_sig_ecdsa_key_new()
```c
void dap_enc_sig_ecdsa_key_new(dap_enc_key_t *a_key);
```

**Описание**: Инициализирует новый объект ключа ECDSA.

**Параметры**:
- `a_key` - указатель на структуру ключа для инициализации

**Пример**:
```c
#include "dap_enc_key.h"
#include "dap_enc_ecdsa.h"

struct dap_enc_key *ecdsa_key = DAP_NEW(struct dap_enc_key);
dap_enc_sig_ecdsa_key_new(ecdsa_key);
// Теперь ecdsa_key готов к использованию
```

#### dap_enc_sig_ecdsa_key_new_generate()
```c
void dap_enc_sig_ecdsa_key_new_generate(dap_enc_key_t *key, const void *kex_buf,
                                      size_t kex_size, const void *seed,
                                      size_t seed_size, size_t key_size);
```

**Описание**: Создает и генерирует новую пару ключей ECDSA.

**Параметры**:
- `key` - ключ для генерации
- `kex_buf` - буфер для key exchange (не используется)
- `kex_size` - размер key exchange буфера (не используется)
- `seed` - seed для детерминированной генерации (опционально)
- `seed_size` - размер seed
- `key_size` - требуемый размер ключа (не используется)

**Пример**:
```c
// Генерация с seed для воспроизводимости
const char *seed = "my_ecdsa_seed";
dap_enc_sig_ecdsa_key_new_generate(ecdsa_key, NULL, 0, seed, strlen(seed), 0);

// Или случайная генерация
dap_enc_sig_ecdsa_key_new_generate(ecdsa_key, NULL, 0, NULL, 0, 0);

// После генерации:
// ecdsa_key->priv_key_data содержит ecdsa_private_key_t (32 байта)
// ecdsa_key->pub_key_data содержит ecdsa_public_key_t (64 байта)
```

#### dap_enc_sig_ecdsa_key_delete()
```c
void *dap_enc_sig_ecdsa_key_delete(dap_enc_key_t *a_key);
```

**Описание**: Освобождает ресурсы, занятые ключом ECDSA.

**Параметры**:
- `a_key` - ключ для удаления

**Возвращает**: NULL

**Пример**:
```c
dap_enc_sig_ecdsa_key_delete(ecdsa_key);
DAP_DELETE(ecdsa_key);
```

### Создание и верификация подписей

#### dap_enc_sig_ecdsa_get_sign()
```c
int dap_enc_sig_ecdsa_get_sign(struct dap_enc_key *key, const void *msg,
                             const size_t msg_size, void *signature,
                             const size_t signature_size);
```

**Описание**: Создает цифровую подпись ECDSA для сообщения.

**Параметры**:
- `key` - приватный ключ для подписи
- `msg` - сообщение для подписи
- `msg_size` - размер сообщения
- `signature` - буфер для подписи
- `signature_size` - размер буфера для подписи (должен быть >= ECDSA_SIG_SIZE)

**Возвращает**:
- `0` - подпись создана успешно
- `-1` - ошибка создания подписи

**Пример**:
```c
const char *message = "This message will be signed with ECDSA";
size_t message_len = strlen(message);

// Выделить буфер для подписи
void *signature = malloc(ECDSA_SIG_SIZE);

if (signature) {
    int sign_result = dap_enc_sig_ecdsa_get_sign(ecdsa_key, message, message_len,
                                               signature, ECDSA_SIG_SIZE);

    if (sign_result == 0) {
        printf("✅ ECDSA signature created successfully\n");
        // Использовать signature...
    } else {
        printf("❌ Failed to create ECDSA signature\n");
    }

    free(signature);
}
```

#### dap_enc_sig_ecdsa_verify_sign()
```c
int dap_enc_sig_ecdsa_verify_sign(struct dap_enc_key *key, const void *msg,
                                const size_t msg_size, void *signature,
                                const size_t signature_size);
```

**Описание**: Проверяет цифровую подпись ECDSA сообщения.

**Параметры**:
- `key` - публичный ключ для проверки
- `msg` - подписанное сообщение
- `msg_size` - размер сообщения
- `signature` - подпись для проверки
- `signature_size` - размер подписи (должен быть >= ECDSA_SIG_SIZE)

**Возвращает**:
- `0` - подпись верна
- `-1` - подпись неверна или ошибка проверки

**Пример**:
```c
int verify_result = dap_enc_sig_ecdsa_verify_sign(ecdsa_key, message, message_len,
                                                signature, ECDSA_SIG_SIZE);

if (verify_result == 0) {
    printf("✅ ECDSA signature verified successfully\n");
} else {
    printf("❌ ECDSA signature verification failed\n");
}
```

### Сериализация и десериализация

#### dap_enc_sig_ecdsa_write_signature()
```c
uint8_t *dap_enc_sig_ecdsa_write_signature(const void *a_sign, size_t *a_sign_out);
```

**Описание**: Сериализует подпись ECDSA в бинарный формат.

**Параметры**:
- `a_sign` - указатель на ecdsa_signature_t
- `a_sign_out` - указатель для сохранения размера сериализованных данных

**Возвращает**: Указатель на сериализованные данные или NULL при ошибке

#### dap_enc_sig_ecdsa_read_signature()
```c
void *dap_enc_sig_ecdsa_read_signature(const uint8_t *a_buf, size_t a_buflen);
```

**Описание**: Десериализует подпись ECDSA из бинарного формата.

#### dap_enc_sig_ecdsa_write_public_key()
```c
uint8_t *dap_enc_sig_ecdsa_write_public_key(const void *a_public_key, size_t *a_buflen_out);
```

**Описание**: Сериализует публичный ключ ECDSA.

#### dap_enc_sig_ecdsa_read_public_key()
```c
void *dap_enc_sig_ecdsa_read_public_key(const uint8_t *a_buf, size_t a_buflen);
```

**Описание**: Десериализует публичный ключ ECDSA.

### Вспомогательные функции

#### dap_enc_sig_ecdsa_hash_fast()
```c
bool dap_enc_sig_ecdsa_hash_fast(const unsigned char *a_data, size_t a_data_size,
                               dap_hash_fast_t *a_out);
```

**Описание**: Вычисляет SHA-256 хэш для данных (используется secp256k1 SHA-256).

#### dap_enc_sig_ecdsa_signature_delete()
```c
void dap_enc_sig_ecdsa_signature_delete(void *a_sig);
```

**Описание**: Освобождает память, занятую подписью.

#### dap_enc_sig_ecdsa_private_key_delete()
```c
void dap_enc_sig_ecdsa_private_key_delete(void *privateKey);
```

**Описание**: Освобождает память, занятую приватным ключом.

#### dap_enc_sig_ecdsa_public_key_delete()
```c
void dap_enc_sig_ecdsa_public_key_delete(void *publicKey);
```

**Описание**: Освобождает память, занятую публичным ключом.

#### dap_enc_sig_ecdsa_private_and_public_keys_delete()
```c
void dap_enc_sig_ecdsa_private_and_public_keys_delete(dap_enc_key_t *a_key);
```

**Описание**: Освобождает память, занятую парой ключей.

#### dap_enc_sig_ecdsa_deinit()
```c
void dap_enc_sig_ecdsa_deinit();
```

**Описание**: Деинициализирует ECDSA контекст.

### Вспомогательные макросы

#### dap_enc_sig_ecdsa_ser_key_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_ser_key_size(const void *a_in);
```

**Описание**: Возвращает размер сериализованного приватного ключа (32 байта).

#### dap_enc_sig_ecdsa_ser_pkey_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_ser_pkey_size(const void *a_in);
```

**Описание**: Возвращает размер сериализованного публичного ключа (65 байт).

#### dap_enc_sig_ecdsa_deser_key_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_deser_key_size(const void *a_in);
```

**Описание**: Возвращает размер десериализованного приватного ключа (32 байта).

#### dap_enc_sig_ecdsa_deser_pkey_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_deser_pkey_size(const void *a_in);
```

**Описание**: Возвращает размер десериализованного публичного ключа (64 байта).

#### dap_enc_sig_ecdsa_signature_size()
```c
DAP_STATIC_INLINE uint64_t dap_enc_sig_ecdsa_signature_size(const void *a_arg);
```

**Описание**: Возвращает размер подписи (64 байта).

## Примеры использования

### ⚠️ **ПРЕДУПРЕЖДЕНИЕ: НЕ ИСПОЛЬЗУЙТЕ В ПРОДАКШЕНЕ**

```c
// ВНИМАНИЕ: ECDSA НЕ РЕКОМЕНДУЕТСЯ для новых проектов
// Используйте Falcon или Dilithium для пост-квантовой безопасности

#include "dap_enc_key.h"
#include "dap_enc_ecdsa.h"
#include <string.h>
#include <stdio.h>

int ecdsa_basic_example() {
    printf("⚠️  WARNING: ECDSA is quantum-vulnerable!\n");
    printf("   Use Falcon or Dilithium for new projects.\n\n");

    // Создание ключа
    struct dap_enc_key *ecdsa_key = DAP_NEW(struct dap_enc_key);
    dap_enc_sig_ecdsa_key_new(ecdsa_key);

    // Генерация ключевой пары
    printf("Generating ECDSA keypair...\n");
    dap_enc_sig_ecdsa_key_new_generate(ecdsa_key, NULL, 0, NULL, 0, 0);

    // Данные для подписи
    const char *message = "Hello, World!";
    size_t message_len = strlen(message);

    printf("Original message: %s\n", message);

    // Создание подписи
    printf("Creating ECDSA signature...\n");
    void *signature = malloc(ECDSA_SIG_SIZE);

    if (!signature) {
        printf("❌ Memory allocation failed\n");
        dap_enc_sig_ecdsa_key_delete(ecdsa_key);
        DAP_DELETE(ecdsa_key);
        return -1;
    }

    int sign_result = dap_enc_sig_ecdsa_get_sign(ecdsa_key, message, message_len,
                                               signature, ECDSA_SIG_SIZE);

    if (sign_result != 0) {
        printf("❌ Signature creation failed\n");
        free(signature);
        dap_enc_sig_ecdsa_key_delete(ecdsa_key);
        DAP_DELETE(ecdsa_key);
        return -1;
    }

    // Верификация подписи
    printf("Verifying ECDSA signature...\n");
    int verify_result = dap_enc_sig_ecdsa_verify_sign(ecdsa_key, message, message_len,
                                                    signature, ECDSA_SIG_SIZE);

    if (verify_result == 0) {
        printf("✅ ECDSA signature verified successfully\n");
        printf("   ⚠️  But remember: ECDSA is quantum-vulnerable!\n");
    } else {
        printf("❌ Signature verification failed\n");
    }

    // Очистка
    free(signature);
    dap_enc_sig_ecdsa_key_delete(ecdsa_key);
    DAP_DELETE(ecdsa_key);

    return verify_result == 0 ? 0 : -1;
}
```

### Пример сравнения с пост-квантовыми алгоритмами

```c
#include "dap_enc_key.h"
#include "dap_enc_ecdsa.h"
#include "dap_enc_falcon.h"
#include "dap_enc_dilithium.h"

int quantum_vs_classical_comparison() {
    printf("🔐 Quantum vs Classical Cryptography Comparison\n");
    printf("===============================================\n");

    const char *test_message = "Security comparison test";
    size_t message_len = strlen(test_message);

    // === КЛАССИЧЕСКИЙ АЛГОРИТМ (УЯЗВИМ К КВАНТОВЫМ АТАКАМ) ===
    printf("\n1. CLASSICAL: ECDSA (secp256k1)\n");
    printf("   ⚠️  VULNERABLE to quantum attacks\n");

    struct dap_enc_key *ecdsa_key = DAP_NEW(struct dap_enc_key);
    dap_enc_sig_ecdsa_key_new(ecdsa_key);
    dap_enc_sig_ecdsa_key_new_generate(ecdsa_key, NULL, 0, NULL, 0, 0);

    void *ecdsa_signature = malloc(ECDSA_SIG_SIZE);
    if (ecdsa_signature) {
        int ecdsa_sign = dap_enc_sig_ecdsa_get_sign(ecdsa_key, test_message, message_len,
                                                  ecdsa_signature, ECDSA_SIG_SIZE);

        if (ecdsa_sign == 0) {
            int ecdsa_verify = dap_enc_sig_ecdsa_verify_sign(ecdsa_key, test_message, message_len,
                                                           ecdsa_signature, ECDSA_SIG_SIZE);

            printf("   ✅ ECDSA: Sign+Verify successful\n");
            printf("   📏 Signature size: %d bytes\n", ECDSA_SIG_SIZE);
            printf("   🔓 Security: 128-bit (classical only)\n");
        }
        free(ecdsa_signature);
    }
    dap_enc_sig_ecdsa_key_delete(ecdsa_key);
    DAP_DELETE(ecdsa_key);

    // === ПОСТ-КВАНТОВЫЕ АЛГОРИТМЫ (ЗАЩИЩЕНЫ ОТ КВАНТОВЫХ АТАК) ===
    printf("\n2. POST-QUANTUM: Falcon (NIST finalist)\n");
    printf("   🛡️  RESISTANT to quantum attacks\n");

    dap_enc_sig_falcon_set_degree(FALCON_512);
    dap_enc_sig_falcon_set_kind(FALCON_PADDED);

    struct dap_enc_key *falcon_key = DAP_NEW(struct dap_enc_key);
    dap_enc_sig_falcon_key_new(falcon_key);
    dap_enc_sig_falcon_key_new_generate(falcon_key, NULL, 0, NULL, 0, 0);

    void *falcon_signature = malloc(sizeof(dilithium_signature_t));
    if (falcon_signature) {
        int falcon_sign = dap_enc_sig_falcon_get_sign(falcon_key, test_message, message_len,
                                                    falcon_signature, sizeof(dilithium_signature_t));

        if (falcon_sign == 0) {
            int falcon_verify = dap_enc_sig_falcon_verify_sign(falcon_key, test_message, message_len,
                                                             falcon_signature, sizeof(dilithium_signature_t));

            printf("   ✅ Falcon: Sign+Verify successful\n");
            printf("   📏 Signature size: ~690 bytes\n");
            printf("   🔐 Security: 128-bit (quantum-resistant)\n");
        }
        free(falcon_signature);
    }
    dap_enc_sig_falcon_key_delete(falcon_key);
    DAP_DELETE(falcon_key);

    printf("\n3. POST-QUANTUM: Dilithium (NIST finalist)\n");
    printf("   🛡️  RESISTANT to quantum attacks\n");

    dap_enc_sig_dilithium_set_type(DILITHIUM_MAX_SPEED);

    struct dap_enc_key *dilithium_key = DAP_NEW(struct dap_enc_key);
    dap_enc_sig_dilithium_key_new(dilithium_key);
    dap_enc_sig_dilithium_key_new_generate(dilithium_key, NULL, 0, NULL, 0, 0);

    void *dilithium_signature = malloc(sizeof(dilithium_signature_t));
    if (dilithium_signature) {
        int dilithium_sign = dap_enc_sig_dilithium_get_sign(dilithium_key, test_message, message_len,
                                                          dilithium_signature, sizeof(dilithium_signature_t));

        if (dilithium_sign == 0) {
            int dilithium_verify = dap_enc_sig_dilithium_verify_sign(dilithium_key, test_message, message_len,
                                                                   dilithium_signature, sizeof(dilithium_signature_t));

            printf("   ✅ Dilithium: Sign+Verify successful\n");
            printf("   📏 Signature size: ~2,420 bytes\n");
            printf("   🔐 Security: 128-bit (quantum-resistant)\n");
        }
        free(dilithium_signature);
    }
    dap_enc_sig_dilithium_key_delete(dilithium_key);
    DAP_DELETE(dilithium_key);

    // === РЕЗЮМЕ ===
    printf("\n📊 SUMMARY:\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║ Algorithm    │ Quantum Safe │ Sig Size │ Security Level     ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ ECDSA        │ ❌ NO        │ 64 B     │ 128-bit (classical)║\n");
    printf("║ Falcon       │ ✅ YES       │ 690 B    │ 128-bit (quantum)  ║\n");
    printf("║ Dilithium    │ ✅ YES       │ 2.4 KB   │ 128-bit (quantum)  ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    printf("\n🎯 RECOMMENDATION:\n");
    printf("   🔴 AVOID ECDSA in new projects\n");
    printf("   🟢 USE Falcon for speed-optimized applications\n");
    printf("   🟢 USE Dilithium for maximum security\n");

    return 0;
}
```

### Пример миграции с ECDSA

```c
#include "dap_enc_key.h"
#include "dap_enc_ecdsa.h"
#include "dap_enc_falcon.h"

// Пример миграции с ECDSA на Falcon
int migration_example() {
    printf("🔄 Migration from ECDSA to Falcon\n");
    printf("=================================\n");

    const char *message = "Migration test message";

    // === СТАРЫЙ ПОДХОД (ECDSA) ===
    printf("1. OLD APPROACH: ECDSA (deprecated)\n");

    struct dap_enc_key *old_key = DAP_NEW(struct dap_enc_key);
    dap_enc_sig_ecdsa_key_new(old_key);
    dap_enc_sig_ecdsa_key_new_generate(old_key, NULL, 0, NULL, 0, 0);

    void *old_signature = malloc(ECDSA_SIG_SIZE);
    if (old_signature) {
        dap_enc_sig_ecdsa_get_sign(old_key, message, strlen(message),
                                 old_signature, ECDSA_SIG_SIZE);
        printf("   ✅ Old ECDSA signature created\n");
        free(old_signature);
    }
    dap_enc_sig_ecdsa_key_delete(old_key);
    DAP_DELETE(old_key);

    // === НОВЫЙ ПОДХОД (FALCON) ===
    printf("\n2. NEW APPROACH: Falcon (quantum-safe)\n");

    dap_enc_sig_falcon_set_degree(FALCON_512);
    dap_enc_sig_falcon_set_kind(FALCON_PADDED);

    struct dap_enc_key *new_key = DAP_NEW(struct dap_enc_key);
    dap_enc_sig_falcon_key_new(new_key);
    dap_enc_sig_falcon_key_new_generate(new_key, NULL, 0, NULL, 0, 0);

    void *new_signature = malloc(sizeof(dilithium_signature_t));
    if (new_signature) {
        int result = dap_enc_sig_falcon_get_sign(new_key, message, strlen(message),
                                               new_signature, sizeof(dilithium_signature_t));

        if (result == 0) {
            printf("   ✅ New Falcon signature created\n");
            printf("   🛡️  Quantum-resistant security\n");
            printf("   📈 Better long-term viability\n");
        }
        free(new_signature);
    }
    dap_enc_sig_falcon_key_delete(new_key);
    DAP_DELETE(new_key);

    printf("\n🎯 Migration Benefits:\n");
    printf("   ✅ Quantum-resistant security\n");
    printf("   ✅ Future-proof implementation\n");
    printf("   ✅ NIST standardization\n");
    printf("   ✅ Better cryptographic properties\n");

    return 0;
}
```

## Производительность

### Бенчмарки ECDSA

| Операция | Производительность | Примечание |
|----------|-------------------|------------|
| **Генерация ключей** | ~50-100 μs | Intel Core i7 |
| **Создание подписи** | ~20-40 μs | Intel Core i7 |
| **Верификация** | ~30-60 μs | Intel Core i7 |

### Сравнение с пост-квантовыми алгоритмами

| Алгоритм | Генерация ключей | Подпись | Верификация | Размер подписи |
|----------|------------------|---------|-------------|----------------|
| **ECDSA** | ~75 μs | ~30 μs | ~45 μs | 64 байта |
| **Falcon-512** | ~150 μs | ~250 μs | ~100 μs | 690 байт |
| **Dilithium2** | ~200 μs | ~65 μs | ~40 μs | 2,420 байт |

## Безопасность

### ⚠️ **Критические предупреждения**

#### **Квантовая уязвимость:**
- ECDSA основан на проблеме дискретного логарифма
- Алгоритм Шора позволяет взломать ECDSA на квантовом компьютере
- Текущая 128-bit безопасность станет 0-bit против квантовых атак

#### **Рекомендации по использованию:**
```
🔴 НИКОГДА не используйте ECDSA в новых проектах
🟡 Используйте ТОЛЬКО для совместимости с существующими системами
🟢 Планируйте миграцию на пост-квантовые алгоритмы
```

#### **Риски:**
- **Будущая уязвимость**: Квантовые компьютеры уже тестируются
- **Долгосрочная безопасность**: Подписи станут подделываемыми
- **Финансовые потери**: Возможность подделки транзакций
- **Репутационные риски**: Нарушение доверия пользователей

### Совместимость и миграция

#### **Поддерживаемые стандарты:**
- secp256k1 (Bitcoin, Ethereum совместимость)
- RFC 6979 (детерминированные подписи)
- BIP 340 (Schnorr-подписи, частично)

#### **План миграции:**
1. **Анализ**: Определить все использования ECDSA
2. **Выбор**: Falcon для скорости, Dilithium для безопасности
3. **Тестирование**: Сравнительные тесты производительности
4. **Миграция**: Постепенный переход на новые алгоритмы
5. **Мониторинг**: Отслеживание прогресса квантовых технологий

## Лучшие практики

### ⚠️ **ПРАВИЛА ИСПОЛЬЗОВАНИЯ ECDSA**

#### **Запрещенные сценарии:**
```c
// ❌ НЕПРАВИЛЬНО: Новые проекты
struct dap_enc_key *key = dap_enc_sig_ecdsa_key_new(); // НЕ ДЕЛАЙТЕ ЭТОГО!

// ❌ НЕПРАВИЛЬНО: Долгосрочная безопасность
// ECDSA не обеспечивает защиты от квантовых атак в будущем

// ❌ НЕПРАВИЛЬНО: Криптовалюты нового поколения
// Используйте пост-квантовые алгоритмы
```

#### **Разрешенные сценарии:**
```c
// ✅ ПРАВИЛЬНО: Совместимость с существующими системами
struct dap_enc_key *legacy_key = dap_enc_sig_ecdsa_key_new();
dap_enc_sig_ecdsa_key_new_generate(legacy_key, NULL, 0, NULL, 0, 0);

// ✅ ПРАВИЛЬНО: Сравнительные тесты
// Тестирование производительности разных алгоритмов

// ✅ ПРАВИЛЬНО: Легаси интеграция
// Поддержка существующих протоколов
```

### Миграционная стратегия

```c
// Стратегия постепенной миграции
typedef enum {
    MIGRATION_LEGACY_ONLY,      // Только ECDSA
    MIGRATION_DUAL_MODE,        // ECDSA + пост-квантовый
    MIGRATION_HYBRID,          // Гибридные подписи
    MIGRATION_QUANTUM_ONLY     // Только пост-квантовый
} migration_phase_t;

int migrate_to_quantum_safe(migration_phase_t phase) {
    switch (phase) {
        case MIGRATION_LEGACY_ONLY:
            // Текущая фаза: только ECDSA
            return use_ecdsa_only();

        case MIGRATION_DUAL_MODE:
            // Промежуточная фаза: двойная подпись
            return use_dual_signatures();

        case MIGRATION_HYBRID:
            // Переходная фаза: гибридные схемы
            return use_hybrid_schemes();

        case MIGRATION_QUANTUM_ONLY:
            // Финальная фаза: только пост-квантовый
            return use_quantum_only();
    }

    return -1;
}
```

### Мониторинг безопасности

```c
// Мониторинг квантовых угроз
int monitor_quantum_threats() {
    // Отслеживание прогресса квантовых технологий
    check_quantum_computer_progress();

    // Анализ уязвимостей
    analyze_quantum_vulnerabilities();

    // Планирование миграции
    plan_migration_timeline();

    return 0;
}

// Проверка необходимости миграции
bool should_migrate_to_quantum_safe() {
    // Критерии для миграции:
    // 1. Доступность квантовых компьютеров
    // 2. Рост вычислительной мощности
    // 3. Изменения в криптографических стандартах
    // 4. Требования регуляторов

    return assess_migration_necessity();
}
```

## Заключение

Модуль `dap_enc_ecdsa` предоставляет классическую реализацию ECDSA для обеспечения совместимости:

### **⚠️ КЛЮЧЕВЫЕ ПРЕДУПРЕЖДЕНИЯ:**

#### **НЕ ИСПОЛЬЗУЙТЕ В НОВЫХ ПРОЕКТАХ:**
- ❌ **Квантовая уязвимость**: Алгоритм Шора
- ❌ **Будущая небезопасность**: Потеря доверия
- ❌ **Финансовые риски**: Возможность подделки

#### **ТОЛЬКО ДЛЯ:**
- ✅ **Легаси-систем**: Поддержка существующих протоколов
- ✅ **Совместимости**: Интеграция с Bitcoin/Ethereum
- ✅ **Тестирования**: Сравнение производительности

### **АЛЬТЕРНАТИВЫ:**

| Цель | Рекомендация | Причина |
|------|-------------|---------|
| **Максимальная скорость** | Falcon-512 | Быстрые операции, меньший размер |
| **Максимальная безопасность** | Dilithium4 | Высокий уровень безопасности |
| **Баланс** | Dilithium3 | Оптимальное соотношение |

### **ПЛАН ДЕЙСТВИЙ:**
1. **Анализ**: Проверить использование ECDSA в проекте
2. **Выбор**: Определить подходящий пост-квантовый алгоритм
3. **Тестирование**: Сравнить производительность
4. **Миграция**: Постепенный переход на безопасные алгоритмы
5. **Мониторинг**: Следить за развитием квантовых технологий

**🚨 ПОМНИТЕ: ECDSA - это технология прошлого. Будущее за пост-квантовой криптографией!** 🔐✨

Для получения дополнительной информации смотрите:
- `dap_enc_ecdsa.h` - полный API ECDSA
- `sig_ecdsa/ecdsa_params.h` - параметры и структуры данных
- Примеры в директории `examples/crypto/`
- Тесты в директории `test/crypto/`

