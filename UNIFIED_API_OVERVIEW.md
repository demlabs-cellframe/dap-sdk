# 🔧 Унифицированный API системы подписей DAP SDK

## Основные улучшения

### 🎯 Умные конструкторы с автоматическим определением типа

Теперь один метод `DapSign.create()` может создавать любой тип подписи на основе переданных параметров:

```python
# Одиночная подпись - автоматически определяется тип
signature = DapSign.create(data, key=dilithium_key)

# Мультиподпись - автоматически COMPOSITE
multi_sig = DapSign.create(data, keys=[key1, key2])

# Агрегированная подпись - указываем флаг
agg_sig = DapSign.create(data, keys=chipmunk_keys, aggregated=True)
```

### 🔍 Универсальная проверка возможностей

Один метод `check_capability()` заменил множество отдельных методов:

```python
signature = DapSign.create(data, key=key)

# Вместо множества методов - один универсальный
is_quantum = signature.check_capability('quantum_secure')
supports_multi = signature.check_capability('multi_signature')
supports_agg = signature.check_capability('aggregated')

# Старые методы остались для удобства
is_quantum = signature.is_quantum_secure()  # использует check_capability()
```

### ✅ Унифицированная верификация

Один метод `verify()` работает для всех типов подписей:

```python
# Одиночная подпись - использует сохранённый ключ
valid = signature.verify(data)

# Мультиподпись - использует сохранённые ключи
valid = multi_signature.verify(data)

# Можно явно передать ключи
valid = signature.verify(data, key=verification_key)
valid = multi_signature.verify(data, keys=verification_keys)
```

### ⚡ Перегруженные quick функции

Универсальные функции с гибкими параметрами:

```python
# Универсальная quick_sign - работает для всех типов
single = quick_sign(data, key=key)
multi = quick_sign(data, keys=[key1, key2])
agg = quick_sign(data, keys=chipmunk_keys, aggregated=True)

# Специализированные функции для ясности
composite = quick_composite_sign(data, keys)
aggregated = quick_aggregated_sign(data, chipmunk_keys)

# Универсальная верификация
valid = quick_verify(signature, data)  # автоматически определяет тип
```

## 🚀 Сравнение старого и нового API

### Старый API (фрагментированный):
```python
# Создание разных типов подписей - разные методы
single = DapSign.create(key, data, sign_type)
multi = DapSign.create_multi_signature(keys, data, sign_type, aggregated)

# Верификация - разные методы
single.verify(key, data)
multi.verify_multi_signature(data)

# Проверка возможностей - множество методов
single.is_quantum_secure()
single.supports_multi_signature()
single.supports_aggregation()
```

### Новый API (унифицированный):
```python
# Создание любого типа подписей - один метод
single = DapSign.create(data, key=key)
multi = DapSign.create(data, keys=keys)
agg = DapSign.create(data, keys=chipmunk_keys, aggregated=True)

# Верификация - один метод для всех
single.verify(data)
multi.verify(data)
agg.verify(data)

# Проверка возможностей - один универсальный метод
single.check_capability('quantum_secure')
single.check_capability('multi_signature')
single.check_capability('aggregated')
```

## 🎯 Ключевые преимущества

### 1. **Простота использования**
- Один метод для создания любых подписей
- Автоматическое определение типа на основе параметров
- Единый интерфейс верификации

### 2. **Расширяемость**
- Легко добавлять новые типы подписей
- Универсальные методы работают с новыми типами автоматически
- Метаданные централизованы в `DapSignMetadata`

### 3. **Обратная совместимость**
- Все старые методы остались для совместимости
- Специализированные конструкторы для ясности кода
- Постепенная миграция на новый API

### 4. **Единообразие**
- Один стиль для всех операций с подписями
- Предсказуемое поведение методов
- Меньше ошибок при использовании

## 🔮 Готовность к будущему

Система готова к добавлению:
- **Кольцевых подписей**: `DapSign.create(data, ring_keys=keys)`
- **Zero-knowledge**: `DapSign.create(data, key=key, zero_knowledge=True)`
- **Пороговых подписей**: `DapSign.create(data, keys=keys, threshold=3)`

Все новые типы автоматически получат поддержку универсальных методов верификации и проверки возможностей.

## 📋 Рекомендации по использованию

1. **Для новых проектов** - используйте унифицированный API
2. **Для миграции** - постепенно переходите на новые методы
3. **Для ясности** - используйте специализированные конструкторы (`create_composite`, `create_aggregated`)
4. **Для гибкости** - используйте универсальный `create()` с параметрами 