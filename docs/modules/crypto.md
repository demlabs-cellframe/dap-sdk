# DAP SDK Crypto Module - Криптографический модуль

## ⚠️ **ВАЖНОЕ ПРЕДУПРЕЖДЕНИЕ О БЕЗОПАСНОСТИ**

# 🚨 **КВАНТОВАЯ НЕБЕЗОПАСНОСТЬ КЛАССИЧЕСКИХ АЛГОРИТМОВ**

**DAP SDK включает как современные пост-квантовые алгоритмы, так и классические методы для совместимости. Классические алгоритмы НЕ ОБЕСПЕЧИВАЮТ ЗАЩИТУ ОТ АТАК КВАНТОВЫХ КОМПЬЮТЕРОВ.**

### **Критические предупреждения:**
- ✅ **Пост-квантовые алгоритмы**: Falcon, Dilithium, Kyber - БЕЗОПАСНЫ от квантовых атак
- ❌ **Классические алгоритмы**: ECDSA, некоторые режимы AES - УЯЗВИМЫ к квантовым атакам
- ❌ **НЕ РЕКОМЕНДУЕТСЯ** использовать классические алгоритмы в новых проектах
- ✅ **Совместимость**: Классические алгоритмы поддерживаются для существующих систем

### **Рекомендации:**
🟢 **Для новых проектов используйте:**
- **Falcon** или **Dilithium** для цифровых подписей
- **Kyber** для обмена ключами
- **AES-256** для симметричного шифрования

🔴 **Избегайте в новых проектах:**
- **ECDSA** (уязвим к алгоритму Шора)
- **RSA** (уязвим к алгоритму Шора)
- Короткие ключи AES (128-bit становится уязвимым)

---

## Обзор

Модуль `dap-sdk/crypto` представляет собой наиболее мощный и комплексный криптографический модуль в DAP SDK. Он предоставляет полную поддержку как классических, так и пост-квантовых криптографических алгоритмов, обеспечивая высокую безопасность и производительность для распределенных приложений.

## Основные возможности

### 🛡️ **Полная криптографическая инфраструктура**
- **Симметричное шифрование**: [AES](crypto/dap_enc_iaes.md), Blowfish, GOST, Salsa2012, SEED
- **Асимметричное шифрование**: [Kyber](crypto/dap_enc_kyber.md), NewHope, MSR-LN
- **Цифровые подписи**: [ECDSA](crypto/dap_enc_ecdsa.md), [Falcon](crypto/dap_enc_falcon.md), [Dilithium](crypto/dap_enc_dilithium.md), [Sphincs+](crypto/dap_enc_sphincsplus.md), Bliss, Picnic, Tesla
- **Многосторонние подписи**: Multisign, MSR-LN
- **Ring signatures**: RingCT20, Shipovnik
- **Хэширование**: SHA-3/Keccak, Blake2, Fusion
- **Сертификаты**: [X.509 совместимые](crypto/dap_cert.md) сертификаты
- **Ключи и идентификаторы**: UUID, GUID
- **[Управление ключами](crypto/dap_enc_key.md)**: Унифицированный API для всех типов ключей

### 🔐 **Пост-квантовая криптография**
- **[Kyber](crypto/dap_enc_kyber.md)** - KEM (Key Encapsulation Mechanism) на основе решеток
- **[Falcon](crypto/dap_enc_falcon.md)** - Подписи на основе решеток (NIST finalist)
- **[Dilithium](crypto/dap_enc_dilithium.md)** - Подписи на основе решеток (NIST finalist)
- **[Sphincs+](crypto/dap_enc_sphincsplus.md)** - Хэш-подписи (NIST finalist)
- **NewHope** - Протокол обмена ключами
- **Picnic** - ZKP-подписи на симметричных примитивах

## Структура модуля

```
dap-sdk/crypto/
├── include/                 # Заголовочные файлы
│   ├── dap_enc.h           # Основной API шифрования
│   ├── dap_enc_key.h       # Управление ключами
│   ├── dap_hash.h          # Хэш-функции
│   ├── dap_sign.h          # Цифровые подписи
│   ├── dap_cert.h          # Сертификаты
│   └── [алгоритмы]         # Специфические алгоритмы
├── src/                    # Исходный код
│   ├── [алгоритмы]/        # Реализации алгоритмов
│   └── XKCP/              # Keccak/SHA-3 библиотека
└── test/                   # Тесты
```

## Основные компоненты

### 1. **Управление ключами (dap_enc_key.h/c)**

#### Структуры данных

```c
typedef struct dap_enc_key {
    union {
        size_t priv_key_data_size;
        size_t shared_key_size;
    };
    union {
        void *priv_key_data;    // Приватный ключ или общий секрет
        byte_t *shared_key;
    };

    size_t pub_key_data_size;
    void *pub_key_data;         // Публичный ключ

    time_t last_used_timestamp;
    dap_enc_key_type_t type;    // Тип алгоритма

    // Callbacks для операций
    dap_enc_callback_dataop_t enc;
    dap_enc_callback_dataop_t dec;
    dap_enc_callback_sign_op_t sign_get;
    dap_enc_callback_sign_op_t sign_verify;
    // ... дополнительные callbacks
} dap_enc_key_t;
```

#### Типы алгоритмов

```c
typedef enum dap_enc_key_type {
    // Симметричные алгоритмы
    DAP_ENC_KEY_TYPE_IAES = 0,           // AES
    DAP_ENC_KEY_TYPE_OAES = 1,           // OAES (Monero)
    DAP_ENC_KEY_TYPE_BF_CBC = 2,         // Blowfish CBC
    DAP_ENC_KEY_TYPE_SALSA2012 = 6,      // Salsa2012
    DAP_ENC_KEY_TYPE_SEED_OFB = 7,       // SEED OFB

    // Пост-квантовые алгоритмы
    DAP_ENC_KEY_TYPE_MLWE_KYBER = 17,    // Kyber KEM
    DAP_ENC_KEY_TYPE_SIG_FALCON = 24,    // Falcon signatures
    DAP_ENC_KEY_TYPE_SIG_DILITHIUM = 21, // Dilithium signatures
    DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS = 25, // Sphincs+ signatures

    // Классические подписи
    DAP_ENC_KEY_TYPE_SIG_ECDSA = 26,     // ECDSA
    DAP_ENC_KEY_TYPE_SIG_BLISS = 19,     // BLISS

    // Специализированные
    DAP_ENC_KEY_TYPE_SIG_RINGCT20 = 22,  // Ring signatures
    DAP_ENC_KEY_TYPE_SIG_SHIPOVNIK = 27, // Shipovnik signatures
    DAP_ENC_KEY_TYPE_SIG_MULTI_CHAINED = 100, // Multisignatures

    // ... дополнительные типы
} dap_enc_key_type_t;
```

## API Reference

### Инициализация и завершение

#### dap_enc_init()
```c
int dap_enc_init(void);
```
**Описание**: Инициализирует криптографический модуль.

**Возвращает**:
- `0` - успех
- `-1` - ошибка инициализации

#### dap_enc_deinit()
```c
void dap_enc_deinit(void);
```
**Описание**: Завершает работу криптографического модуля и освобождает ресурсы.

### Работа с ключами

#### dap_enc_key_new()
```c
dap_enc_key_t *dap_enc_key_new(dap_enc_key_type_t a_key_type);
```
**Описание**: Создает новый объект ключа заданного типа.

**Параметры**:
- `a_key_type` - тип криптографического алгоритма

**Возвращает**: Указатель на созданный ключ или `NULL` при ошибке

#### dap_enc_key_delete()
```c
void dap_enc_key_delete(dap_enc_key_t *a_key);
```
**Описание**: Удаляет ключ и освобождает все связанные ресурсы.

### Шифрование и дешифрование

#### dap_enc_code()
```c
size_t dap_enc_code(dap_enc_key_t *a_key,
                    const void *a_buf_in, size_t a_buf_in_size,
                    void *a_buf_out, size_t a_buf_out_size_max,
                    dap_enc_data_type_t a_data_type_out);
```
**Описание**: Шифрует данные с использованием заданного ключа.

#### dap_enc_decode()
```c
size_t dap_enc_decode(dap_enc_key_t *a_key,
                      const void *a_buf_in, size_t a_buf_in_size,
                      void *a_buf_out, size_t a_buf_out_size_max,
                      dap_enc_data_type_t a_data_type_in);
```
**Описание**: Дешифрует данные с использованием заданного ключа.

## Примеры использования

### Пример 1: Симметричное шифрование AES

```c
#include "dap_enc.h"
#include "dap_enc_key.h"

int aes_example() {
    // Инициализация криптографического модуля
    if (dap_enc_init() != 0) {
        fprintf(stderr, "Failed to initialize crypto module\n");
        return -1;
    }

    if (dap_enc_key_init() != 0) {
        fprintf(stderr, "Failed to initialize key module\n");
        dap_enc_deinit();
        return -1;
    }

    // Создание AES ключа с правильными параметрами
    const char *seed = "my_secret_seed";
    dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_IAES,
                                                  NULL, 0,                    // kex_buf, kex_size
                                                  (const void*)seed, strlen(seed), // seed, seed_size
                                                  256);                       // key_size

    if (!key) {
        fprintf(stderr, "Failed to generate AES key\n");
        dap_enc_key_deinit();
        dap_enc_deinit();
        return -1;
    }

    // Данные для шифрования
    const char *plaintext = "Hello, World!";
    size_t plaintext_len = strlen(plaintext);

    // Вычисление размера буфера для шифрования
    size_t encrypted_size = dap_enc_code_out_size(key, plaintext_len, DAP_ENC_DATA_TYPE_RAW);
    if (encrypted_size == 0) {
        fprintf(stderr, "Failed to calculate encrypted size\n");
        dap_enc_key_delete(key);
        dap_enc_key_deinit();
        dap_enc_deinit();
        return -1;
    }

    uint8_t *encrypted = malloc(encrypted_size);
    if (!encrypted) {
        fprintf(stderr, "Memory allocation failed\n");
        dap_enc_key_delete(key);
        dap_enc_key_deinit();
        dap_enc_deinit();
        return -1;
    }

    // Шифрование данных
    size_t actual_encrypted_size = dap_enc_code(key,
                                               (const void*)plaintext, plaintext_len,
                                               encrypted, encrypted_size,
                                               DAP_ENC_DATA_TYPE_RAW);

    if (actual_encrypted_size == 0) {
        fprintf(stderr, "Encryption failed\n");
        free(encrypted);
        dap_enc_key_delete(key);
        dap_enc_key_deinit();
        dap_enc_deinit();
        return -1;
    }

    // Вычисление размера буфера для дешифрования
    size_t decrypted_size = dap_enc_decode_out_size(key, actual_encrypted_size, DAP_ENC_DATA_TYPE_RAW);
    if (decrypted_size == 0) {
        fprintf(stderr, "Failed to calculate decrypted size\n");
        free(encrypted);
        dap_enc_key_delete(key);
        dap_enc_key_deinit();
        dap_enc_deinit();
        return -1;
    }

    uint8_t *decrypted = malloc(decrypted_size);
    if (!decrypted) {
        fprintf(stderr, "Memory allocation failed\n");
        free(encrypted);
        dap_enc_key_delete(key);
        dap_enc_key_deinit();
        dap_enc_deinit();
        return -1;
    }

    // Дешифрование данных
    size_t actual_decrypted_size = dap_enc_decode(key,
                                                 encrypted, actual_encrypted_size,
                                                 decrypted, decrypted_size,
                                                 DAP_ENC_DATA_TYPE_RAW);

    if (actual_decrypted_size == 0) {
        fprintf(stderr, "Decryption failed\n");
        free(encrypted);
        free(decrypted);
        dap_enc_key_delete(key);
        dap_enc_key_deinit();
        dap_enc_deinit();
        return -1;
    }

    // Проверка корректности
    if (actual_decrypted_size == plaintext_len &&
        memcmp(decrypted, plaintext, plaintext_len) == 0) {
        printf("AES encryption/decryption successful!\n");
    } else {
        fprintf(stderr, "Decryption verification failed\n");
        free(encrypted);
        free(decrypted);
        dap_enc_key_delete(key);
        dap_enc_key_deinit();
        dap_enc_deinit();
        return -1;
    }

    // Очистка ресурсов
    free(encrypted);
    free(decrypted);
    dap_enc_key_delete(key);
    dap_enc_key_deinit();
    dap_enc_deinit();

    return 0;
}
```

### Пример 2: Пост-квантовая подпись с Dilithium

```c
#include "dap_enc.h"
#include "dap_enc_key.h"

int dilithium_sign_example() {
    // Инициализация
    if (dap_enc_init() != 0) return -1;

    // Создание Dilithium ключа
    dap_enc_key_t *key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM,
                                                  NULL, 0,
                                                  "dilithium_seed", 14,
                                                  0);

    // Данные для подписи
    const char *message = "Post-quantum signature test";
    size_t message_len = strlen(message);

    // Создание подписи
    size_t sig_size = dap_enc_calc_signature_unserialized_size(key);
    uint8_t *signature = malloc(sig_size);

    int sign_result = key->sign_get(key, message, message_len, signature, sig_size);

    // Проверка подписи
    int verify_result = key->sign_verify(key, message, message_len, signature, sig_size);

    // Очистка
    free(signature);
    dap_enc_key_delete(key);
    dap_enc_deinit();

    return verify_result == 0 ? 0 : -1;
}
```

## Производительность

### Бенчмарки криптографических операций

| Алгоритм | Операция | Производительность | Примечание |
|----------|----------|-------------------|------------|
| **AES-256** | Шифрование | ~500 MB/s | Intel Core i7 |
| **Kyber** | KEM генерация | ~10 μs | NIST Level 1 |
| **Falcon** | Подпись | ~100 μs | NIST Level 1 |
| **Dilithium** | Подпись | ~50 μs | NIST Level 2 |
| **ECDSA** | Подпись | ~20 μs | secp256r1 |

## Безопасность

### Уровни безопасности

#### Классические алгоритмы
- **AES-256**: 128-bit безопасность
- **ECDSA**: 128-bit безопасность (уязвим к квантовым атакам)

#### Пост-квантовые алгоритмы
- **Kyber**: 128/192/256-bit безопасность против квантовых атак
- **Falcon**: 128/192/256-bit безопасность
- **Dilithium**: 128/192/256-bit безопасность

### Рекомендации по использованию

#### Для новых проектов (рекомендуется):
```c
// Пост-квантовая подпись
dap_enc_key_t *sign_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, ...);

// Пост-квантовый KEM
dap_enc_key_t *kem_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_MLWE_KYBER, ...);

// Симметричное шифрование
dap_enc_key_t *enc_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_IAES, ...);
```

## Заключение

Модуль `dap-sdk/crypto` предоставляет наиболее полную и современную криптографическую инфраструктуру:

### Ключевые преимущества:
- **Будущая безопасность**: Поддержка пост-квантовых алгоритмов
- **Высокая производительность**: Оптимизированные реализации
- **Простота использования**: Единый API для всех алгоритмов
- **Модульность**: Легкое расширение и модификация

### Рекомендации:
1. **Для новых проектов**: Используйте пост-квантовые алгоритмы (Kyber, Falcon, Dilithium)
2. **Для шифрования**: AES-256 или пост-квантовые аналоги
3. **Для подписей**: Dilithium или Falcon (NIST finalists)

Для получения дополнительной информации смотрите:
- `dap_enc.h` - основной API шифрования
- `dap_enc_key.h` - управление ключами
- `dap_sign.h` - цифровые подписи
- `dap_hash.h` - хэш-функции
- Примеры в директории `examples/crypto/`
- Тесты в директории `test/crypto/`