# 🎯 Руководство по единому конструктору DapSign

## 🌟 Концепция

Все операции с подписями теперь выполняются через **единый конструктор `__init__`** с умным определением параметров. Никаких отдельных методов `create()` - всё через один интерфейс!

## 🔧 Четыре режима конструктора

### 1️⃣ Режим 1: Одиночная подпись
```python
# Автоопределение типа по ключу
signature = DapSign(data, key=dilithium_key)
# ↳ Тип подписи = тип ключа (DILITHIUM)

signature = DapSign(data, key=falcon_key)
# ↳ Тип подписи = FALCON
```

### 2️⃣ Режим 2: Мультиподпись с автоопределением
```python
# Смешанные ключи → COMPOSITE
mixed_keys = [dilithium_key, falcon_key]
signature = DapSign(data, keys=mixed_keys)
# ↳ Автоматически COMPOSITE

# Все CHIPMUNK → CHIPMUNK (агрегированная)
chipmunk_keys = [chipmunk_key1, chipmunk_key2]
signature = DapSign(data, keys=chipmunk_keys)
# ↳ Автоматически CHIPMUNK (агрегированная)
```

### 3️⃣ Режим 3: Мультиподпись с явным типом
```python
# Принудительно COMPOSITE
signature = DapSign(data, keys=keys, sign_type=DapSignType.COMPOSITE)

# Принудительно CHIPMUNK (агрегированная)
signature = DapSign(data, keys=chipmunk_keys, sign_type=DapSignType.CHIPMUNK)
```

### 4️⃣ Режим 4: Обертка существующего handle
```python
# Обертка над C-уровневым handle
signature = DapSign(handle=existing_handle, sign_type=DapSignType.DILITHIUM, keys=keys)
```

## 🔐 Реальные типы DAP SDK

Убраны **несуществующие** типы, добавлены только **реальные** из DAP SDK:

### ✅ Поддерживаемые типы:
```python
# Рекомендуемые (квантово-защищённые)
DapSignType.DILITHIUM      # Основной пост-квантовый алгоритм
DapSignType.FALCON        # Альтернативный пост-квантовый
DapSignType.PICNIC        # Пост-квантовый с zero-knowledge
DapSignType.SPHINCSPLUS   # Дополнительный пост-квантовый

# Мультиподписи
DapSignType.COMPOSITE     # Композиционная мультиподпись
DapSignType.CHIPMUNK      # Агрегированная подпись

# Экспериментальные
DapSignType.TESLA         # Экспериментальный алгоритм
DapSignType.SHIPOVNIK     # Экспериментальный алгоритм

# Устаревшие (квантово-уязвимые)
DapSignType.BLISS         # DEPRECATED
DapSignType.ECDSA         # DEPRECATED
```

### ❌ Удалены несуществующие:
- `RSA` - нет в DAP SDK
- `ED25519` - нет в DAP SDK
- `RING_*` - будущие расширения
- `ZK_*` - будущие расширения

## 🧠 Логика автоопределения

### Правила:
1. **Один ключ** → тип подписи = тип ключа
2. **Несколько ключей, все CHIPMUNK** → CHIPMUNK (агрегированная)
3. **Несколько ключей, смешанные** → COMPOSITE

### Примеры автоопределения:
```python
# Одиночные подписи
DapSign(data, key=DapCryptoKey(DapKeyType.DILITHIUM))  # → DILITHIUM
DapSign(data, key=DapCryptoKey(DapKeyType.FALCON))     # → FALCON

# Мультиподписи
chipmunk_keys = [DapCryptoKey(DapKeyType.CHIPMUNK) for _ in range(3)]
DapSign(data, keys=chipmunk_keys)  # → CHIPMUNK (агрегированная)

mixed_keys = [DapCryptoKey(DapKeyType.DILITHIUM), DapCryptoKey(DapKeyType.FALCON)]
DapSign(data, keys=mixed_keys)     # → COMPOSITE
```

## 📝 Гибкость вызова

Единый конструктор поддерживает различные стили:

```python
# Позиционные аргументы
DapSign(data, key)
DapSign(data, key, keys)

# Именованные аргументы
DapSign(data=data, key=key)
DapSign(data=data, keys=keys)
DapSign(data=data, keys=keys, sign_type=DapSignType.COMPOSITE)

# Смешанные
DapSign(data, key=key)
DapSign(data, keys=keys, sign_type=DapSignType.CHIPMUNK)

# Один ключ как список (тоже работает)
DapSign(data, keys=[single_key])  # → тип ключа
```

## ⚡ Quick функции

Все quick функции используют единый конструктор внутри:

```python
# Универсальные
quick_sign(data, key=key)           # → DapSign(data, key=key)
quick_sign(data, keys=keys)         # → DapSign(data, keys=keys)
quick_multi_sign(data, keys)        # → DapSign(data, keys=keys)

# Специализированные
quick_composite_sign(data, keys)    # → DapSign(data, keys=keys, sign_type=COMPOSITE)
quick_aggregated_sign(data, keys)   # → DapSign(data, keys=keys, sign_type=CHIPMUNK)

# Верификация (универсальная)
quick_verify(signature, data)       # → signature.verify(data)
```

## ✅ Универсальная верификация

Один метод `verify()` для всех типов подписей:

```python
# Автоматически использует сохранённые ключи
signature.verify(data)

# Явно указанные ключи
single_signature.verify(data, key=key)
multi_signature.verify(data, keys=keys)
```

## 🎯 Преимущества единого конструктора

### 1. **Простота**
- Один метод для всех операций
- Не нужно запоминать разные методы
- Интуитивно понятный интерфейс

### 2. **Умность**
- Автоматическое определение типа
- Работает "как ожидается"
- Минимум параметров

### 3. **Гибкость**
- Поддержка разных стилей вызова
- Явное и автоматическое управление
- Расширяемость для новых типов

### 4. **Единообразие**
- Один паттерн для всех случаев
- Предсказуемое поведение
- Лёгкое тестирование

## 📚 Миграция

### Было (сложно):
```python
# Разные методы для разных операций
signature = DapSign.create(data, key=key, sign_type=DapSignType.DILITHIUM)
multi_sig = DapSign.create_multi(data, keys, DapSignType.COMPOSITE, aggregated=False)
agg_sig = DapSign.create_aggregated(data, chipmunk_keys)
```

### Стало (просто):
```python
# Один конструктор для всего
signature = DapSign(data, key=key)              # Автоопределение
multi_sig = DapSign(data, keys=keys)            # Автоопределение
agg_sig = DapSign(data, keys=chipmunk_keys)     # Автоопределение
```

## 🚀 Результат

Достигнут **идеальный баланс**:
- **Максимальная простота** использования
- **Полная функциональность** всех типов подписей  
- **Умное поведение** без лишних параметров
- **Единый интерфейс** для всех операций

**Единый конструктор DapSign** - это элегантное решение, которое делает работу с криптографией в DAP SDK интуитивно понятной! 🎉 