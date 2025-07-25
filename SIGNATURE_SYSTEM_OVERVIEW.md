# 🔐 Единая система подписей DAP SDK

## Обзор архитектуры

Новая система подписей DAP SDK объединяет все типы подписей в единый интерфейс с богатыми метаданными и расширенными возможностями.

## ✨ Ключевые особенности

### 🎯 Единый класс `DapSign`
- Поддерживает все типы подписей: обычные, мультиподписи, кольцевые, с нулевым разглашением
- Автоматическое определение возможностей на основе типа подписи
- Встроенные метаданные для каждого типа

### 📊 Система метаданных `DapSignMetadata`
- **Квантовая безопасность**: автоматическая проверка устойчивости к квантовым атакам
- **Статус deprecated**: маркировка устаревших алгоритмов
- **Возможности**: мультиподписи, агрегация, кольцевые подписи, нулевое разглашение
- **Технические характеристики**: размеры ключей и подписей

### 🔒 Типы подписей

#### Рекомендуемые (квантово-защищённые)
- **DILITHIUM** - основной пост-квантовый алгоритм
- **FALCON** - альтернативный пост-квантовый алгоритм  
- **PICNIC** - пост-квантовый алгоритм с нулевым разглашением
- **COMPOSITE** - композиционная мультиподпись
- **CHIPMUNK** - бурундучья подпись (поддерживает агрегацию)

#### Будущие расширения
- **RING_DILITHIUM** - кольцевые подписи на базе Dilithium
- **RING_FALCON** - кольцевые подписи на базе Falcon
- **ZK_DILITHIUM** - Dilithium с нулевым разглашением
- **ZK_FALCON** - Falcon с нулевым разглашением

#### Устаревшие (квантово-уязвимые)
- **BLISS** - DEPRECATED
- **RSA** - DEPRECATED  
- **ECDSA** - DEPRECATED
- **ED25519** - DEPRECATED

## 🚀 API примеры

### Создание обычной подписи
```python
from dap.crypto import DapSign, DapCryptoKey, DapKeyType, DapSignType

# Создание ключа
key = DapCryptoKey(DapKeyType.DILITHIUM)

# Создание подписи
signature = DapSign.create(key, "данные для подписи")

# Проверка метаданных
print(f"Квантово-защищённая: {signature.is_quantum_secure()}")
print(f"Устаревшая: {signature.is_deprecated()}")
```

### Создание мультиподписи
```python
# Композиционная мультиподпись
keys = [DapCryptoKey(DapKeyType.DILITHIUM), DapCryptoKey(DapKeyType.FALCON)]
multi_sign = DapSign.create_multi_signature(keys, "документ", DapSignType.COMPOSITE)

# Агрегированная подпись (только CHIPMUNK)
chipmunk_keys = [DapCryptoKey(DapKeyType.CHIPMUNK) for _ in range(3)]
agg_sign = DapSign.create_multi_signature(
    chipmunk_keys, "документ", 
    DapSignType.CHIPMUNK, aggregated=True
)
```

### Проверка совместимости
```python
from dap.crypto import check_signature_compatibility, get_recommended_signature_types

# Проверка возможностей алгоритма
compatibility = check_signature_compatibility(DapSignType.CHIPMUNK)
print(f"Поддерживает агрегацию: {compatibility['aggregated']}")

# Получение рекомендуемых типов
recommended = get_recommended_signature_types()
print(f"Рекомендуемые типы: {[t.value for t in recommended]}")
```

## 🔧 Расширенные возможности

### Автоматическое определение типа
```python
# Тип подписи автоматически определяется по ключу
key = DapCryptoKey(DapKeyType.FALCON)
signature = quick_sign(key, "данные")  # Автоматически FALCON подпись
```

### Проверка метаданных
```python
signature = quick_sign(key, "данные")

# Проверка всех возможностей
print(f"Мультиподпись: {signature.supports_multi_signature()}")
print(f"Агрегация: {signature.supports_aggregation()}")
print(f"Кольцевые подписи: {signature.supports_ring_signature()}")
print(f"Нулевое разглашение: {signature.supports_zero_knowledge()}")
```

### Верификация мультиподписей
```python
# Автоматическая верификация на основе типа
multi_sign = DapSign.create_multi_signature(keys, data, DapSignType.COMPOSITE)
is_valid = multi_sign.verify_multi_signature(data)
```

## 🛠 Интеграция с C биндингами

Система автоматически использует соответствующие C функции:
- `py_dap_crypto_multi_sign_*` для композиционных подписей
- `py_dap_crypto_aggregated_sign_*` для агрегированных подписей
- Автоматическое управление памятью через контекстные менеджеры

## 🔮 Будущие расширения

1. **Кольцевые подписи** - анонимные подписи в группе
2. **Zero-knowledge proofs** - подписи с нулевым разглашением
3. **Threshold signatures** - пороговые подписи
4. **Blind signatures** - слепые подписи
5. **Group signatures** - групповые подписи

Система спроектирована для легкого добавления новых типов подписей без изменения существующего API. 