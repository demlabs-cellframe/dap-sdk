# Криптографическая модель DAP Stream

> **Версия:** 1.0 | **Дата:** 2026-07-20 | **Модуль:** `dap-sdk/crypto/`, `dap-sdk/net/trans/`

## Обзор

DAP Stream использует многослойную криптографическую модель с тремя уровнями ключей. Обфускация защищает от DPI, DSHP handshake обеспечивает обмен ключами, а stream encryption шифрует данные.

## Три уровня ключей

```
┌─────────────────────────────────────────────────────────────┐
│ Уровень 1: session_key_open (асимметричный, KEM)            │
│   Kyber / Falcon / ECDSA / MSRLN                            │
│   Обмен публичными ключами между Alice и Bob                │
│   → Общий секрет (shared secret)                            │
├─────────────────────────────────────────────────────────────┤
│ Уровень 2: session_key (симметричный, сессионный)           │
│   Производится из общего секрета через KDF                   │
│   Используется для шифрования DSHP сообщений после handshake│
├─────────────────────────────────────────────────────────────┤
│ Уровень 3: stream_key (шифрование потока)                   │
│   Производится из session_key                               │
│   Шифрует/дешифрует stream пакеты (dap_stream_pkt_t)        │
└─────────────────────────────────────────────────────────────┘
```

### Поток ключей

```
Alice                          Bob
  │                              │
  │  pub_key_alice ─────────────→│
  │                              │ shared_secret = KEM(alice_pub, bob_priv)
  │←───────────── pub_key_bob    │
  │                              │
  shared_secret = KEM(bob_pub, alice_priv)
  │                              │
  session_key = KDF(shared_secret)
  stream_key  = KDF(session_key)
  │                              │
  │  encrypted stream data ←────→│
```

## Ключевая структура: dap_enc_key_t

`dap_enc_key_t` — универсальная обёртка для всех алгоритмов шифрования. Поля `priv_key_data` и `shared_key` объединены в union для поддержки как асимметричных ключей, так и общих секретов:

```c
typedef struct dap_enc_key {
    // Размеры ключевых данных (union: для асимметричных или общих секретов)
    union {
        size_t      priv_key_data_size;
        size_t      shared_key_size;
    };
    union {
        void        *priv_key_data;     // Приватный ключ (асимметричные)
        byte_t      *shared_key;        // Общий секрет (симметричные)
    };
    size_t              pub_key_data_size;
    void                *pub_key_data;  // Публичный ключ
    time_t              last_used_timestamp;
    dap_enc_key_type_t  type;           // Тип алгоритма

    // Функциональные указатели — шифрование
    dap_enc_callback_dataop_t     enc;      // Шифрование (authenticated)
    dap_enc_callback_dataop_t     dec;      // Дешифрование (authenticated)
    dap_enc_callback_dataop_na_t  enc_na;   // Шифрование (no-auth)
    dap_enc_callback_dataop_na_t  dec_na;   // Дешифрование (no-auth)
    dap_enc_callback_dataop_na_ext_t dec_na_ext; // Дешифрование (extended)

    // Функциональные указатели — подписи
    dap_enc_callback_sign_op_t    sign_get;     // Генерация подписи
    dap_enc_callback_sign_op_t    sign_verify;  // Проверка подписи

    // Функциональные указатели — KEM (Key Encapsulation)
    dap_enc_gen_alice_shared_key  gen_alice_shared_key; // Alice side KEM
    dap_enc_gen_bob_shared_key    gen_bob_shared_key;   // Bob side KEM

    // Дополнительные поля
    void                *pbk_list_data;     // Данные списка публичных ключей
    size_t              pbk_list_size;
    dap_enc_get_allpbk_list get_all_pbk_list;
    void                *_pvt;              // Приватные данные
    void                *_inheritor;        // Данные наследника
    size_t              _inheritor_size;
} dap_enc_key_t;
```

## Поддерживаемые алгоритмы

### Асимметричные (KEM / подпись)

| Алгоритм | Тип | Описание |
|----------|-----|----------|
| Kyber | KEM | Post-quantum key encapsulation (Kyber512/768/1024) |
| Falcon | Подпись | Post-quantum signature (Falcon-512/1024) |
| Dilithium | Подпись | Post-quantum signature (Dilithium2/3/5) |
| ECDSA | Подпись | Elliptic Curve (secp256k1, P-256) |
| SPHINCS+ | Подпись | Post-quantum hash-based signature |
| MSRLN | KEM | Legacy lattice-based KEM (P2P compatibility) |

### Симметричные

| Алгоритм | Тип | Описание |
|----------|-----|----------|
| AES | Block cipher | AES-128/192/256 (OAES mode) |
| SALSA2012 | Stream cipher | Salsa20/12 (используется в обфускации) |
| GOST | Block cipher | GOST 28147-89 OFB (Magma) / GOST 28147-14 OFB (Kuznechik) |
| Blowfish | Block cipher | Blowfish |
| SEED | Block cipher | Korean SEED cipher |

### Хеширование и KDF

| Алгоритм | Описание |
|----------|----------|
| SHAKE256 | KDF для обфускации (produces nonce + key) |
| SHA-256/512 | Standard hash |
| Streebog | Russian GOST hash |
| BLAKE2 | High-performance hash |
| MD5 | Legacy (JA3 fingerprints) |

## TLS Wrapper

Абстракция для реального TLS (не mimicry) через платформенные API:

| Реализация | Платформа | Модуль |
|-----------|-----------|--------|
| OpenSSL | Linux, BSD, generic | `crypto/tls/os/openssl/dap_tls_openssl.c` |
| Apple Security | macOS, iOS | `crypto/tls/os/apple/dap_tls_apple.c` |
| SChannel | Windows | `crypto/tls/os/windows/dap_tls_schannel.c` |

Интерфейс: `crypto/tls/include/dap_tls.h`

## Обфускация (транспортный уровень)

Обфускация использует SALSA2012 с ключом, производным от размера пакета:

```c
// KDF: seed + "obfuscation" + packet_size → 40 bytes
// [0..7]  = nonce (8 bytes)
// [8..39] = key (32 bytes)
dap_enc_kdf_derive(seed, "obfuscation", packet_size) → [nonce(8), key(32)]

// Шифрование
dap_enc_code(SALSA2012, key, nonce, plaintext) → ciphertext
```

**Свойства:**
- Ключ эфемерный — зависит от размера пакета
- Seed статический: `"cellframe-transport-obfuscation-v1"`
- Не является криптографической защитой — только anti-DPI
- После деобфускации ключ выбрасывается

## Шифрование stream пакетов

### Запись (dap_stream_pkt_write_unsafe)

```c
if (session->key) {
    // Шифрование через enc_na (no-auth) или enc (authenticated)
    session->key->enc_na(session->key, data, size, &encrypted, &encrypted_size);
} else {
    // Без шифрования (копирование)
    memcpy(output, data, size);
}
```

### Чтение (dap_stream_pkt_read_unsafe)

```c
if (session->key) {
    session->key->dec_na(session->key, data, size, &decrypted, &decrypted_size);
} else {
    memcpy(output, data, size);
}
```

**Overhead шифрования:** `DAP_STREAM_PKT_ENCRYPTION_OVERHEAD = 200 bytes` — максимальный overhead от шифрования (padding, nonce, auth tag).

## Шифрование каналов

Канал (`dap_stream_ch_pkt_t`) имеет поле `enc_type` в заголовке:

```c
typedef struct dap_stream_ch_pkt_hdr {
    uint8_t     id;           // ID канала
    uint8_t     enc_type;     // Тип шифрования (0 = не зашифровано)
    uint8_t     type;         // Тип пакета
    uint8_t     padding;
    uint64_t    seq_id;
    uint32_t    data_size;
} __attribute__((packed)) dap_stream_ch_pkt_hdr_t;
```

Если `enc_type != 0`, данные канала шифруются отдельно от stream-level шифрования.

## Ключи в dap_net_trans_ctx_t

Transport context хранит все три уровня ключей:

```c
typedef struct dap_net_trans_ctx {
    dap_enc_key_t *session_key_open;   // Асимметричный KEM
    dap_enc_key_t *session_key;        // Симметричный сессионный
    dap_enc_key_t *stream_key;         // Шифрование потока
    char          *session_key_id;     // Идентификатор ключа
    // ...
} dap_net_trans_ctx_t;
```

## Безопасность

### Что защищает каждый уровень

| Уровень | Защищает от | Не защищает от |
|---------|-------------|----------------|
| Обфускация | DPI, traffic analysis | Целенаправленного дешифрования |
| DSHP handshake | Перехвата ключей | Compromised endpoint |
| Stream encryption | Прослушивания | Compromised endpoint, side-channel |

### Post-quantum readiness

DAP SDK поддерживает post-quantum алгоритмы:
- **Kyber** — KEM (замена ECDH)
- **Falcon / Dilithium** — подписи (замена ECDSA)
- **SPHINCS+** — hash-based подписи (backup)

## Связанные документы

- [03 — DSHP Handshake](03-handshake_ru.md) — протокол обмена ключами
- [04 — Обфускация](../transport/04-obfuscation_ru.md) — транспортная обфускация
- [01 — Stream Protocol](01-stream-protocol_ru.md) — формат пакетов
- Заголовочные файлы: `crypto/include/dap_enc_key.h`, `dap_enc.h`, `crypto/tls/include/dap_tls.h`
