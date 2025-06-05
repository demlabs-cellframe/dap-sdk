# Chipmunk Cryptographic Signatures

Реализация алгоритма криптографических подписей **Chipmunk** для блокчейн-консенсуса Cellframe Node.

## 🎯 Статус: 80% завершен ✅

**Chipmunk = HOTS + Tree + HVC** (НЕ Dilithium!)

### ✅ Работает:
- HOTS модуль (Setup, Keygen, Sign)
- Система сборки и тестирования
- Правильная структура данных

### ⚠️ В отладке:
- HOTS Verify функция (математическое несоответствие)

## 🚀 Быстрый старт

```bash
cd build-debug
cmake .. -DBUILD_TESTING=ON -DBUILD_CRYPTO_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
make chipmunk_hots_test
./dap-sdk/crypto/test/crypto/chipmunk_hots_test
```

## 📚 Полная документация

**➤ [docs/README.md](docs/README.md)** - Навигация по всей документации

### Основные файлы документации:
- **[docs/HOTS_BUILD_AND_TEST.md](docs/HOTS_BUILD_AND_TEST.md)** - Руководство по сборке и тестированию
- **[docs/chipmunk_progress.md](docs/chipmunk_progress.md)** - Подробный трекер прогресса
- **[docs/chipmunk_structure.md](docs/chipmunk_structure.md)** - Архитектурная документация  
- **[docs/chipmunk_documentation.md](docs/chipmunk_documentation.md)** - Техническая документация API

## 📁 Структура файлов

```
chipmunk/
├── README.md                 # Этот файл  
├── docs/                     # 📚 Документация
│   ├── README.md            # Навигация по документации
│   ├── HOTS_BUILD_AND_TEST.md
│   └── chipmunk_progress.md
├── chipmunk_hots.h          # API HOTS модуля
├── chipmunk_hots.c          # Реализация HOTS
├── chipmunk_poly.h          # API полиномов
├── chipmunk_poly.c          # Полиномные операции
├── chipmunk.h               # Основной API (старый)
├── chipmunk.c               # Основная реализация (старый)
└── Chipmunk.orig/           # Оригинальный Rust код
```

## 🔬 Алгоритм HOTS

1. **Setup**: Генерация 6 случайных полиномов a[i]
2. **Keygen**: v0 = Σ(a_i * s0_i), v1 = Σ(a_i * s1_i)  
3. **Sign**: σ[i] = s0[i] * H(m) + s1[i]
4. **Verify**: Σ(a_i * σ_i) ?= H(m) * v0 + v1

---

*Проект: Cellframe Node RC-6.0* 