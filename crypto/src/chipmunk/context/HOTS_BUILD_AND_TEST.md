# HOTS (Chipmunk) - Руководство по сборке и тестированию

## 🚀 Быстрый старт

### Минимальные команды:
```bash
# Из корня проекта
cd build-debug
cmake .. -DBUILD_TESTING=ON -DBUILD_CRYPTO_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
make chipmunk_hots_test
./dap-sdk/crypto/test/crypto/chipmunk_hots_test
```

## 📋 Системные требования

- **OS**: Linux 6.12.22+bpo-amd64
- **Shell**: /usr/bin/bash  
- **Compiler**: gcc/cc
- **Build system**: CMake 3.10+
- **Working directory**: `/home/naeper/work/dap/cellframe-node.rc-6.0`

## 🛠️ Сборка

### 1. Конфигурация CMake
```bash
cd /home/naeper/work/dap/cellframe-node.rc-6.0
mkdir -p build-debug
cd build-debug

# ОБЯЗАТЕЛЬНЫЕ параметры для криптотестов:
cmake .. \
  -DBUILD_TESTING=ON \
  -DBUILD_CRYPTO_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug
```

### 2. Инкрементная сборка
```bash
# Полная сборка библиотек
make -j4

# Только HOTS тест
make chipmunk_hots_test

# Только криптобиблиотека  
make dap_crypto
```

### 3. Проверка сборки
```bash
# Проверить что тест собран
ls -la ./dap-sdk/crypto/test/crypto/chipmunk_hots_test

# Должен вернуть исполняемый файл
```

## 🧪 Запуск тестов

### Основная команда:
```bash
cd build-debug
./dap-sdk/crypto/test/crypto/chipmunk_hots_test
```

### Ожидаемый вывод (начало):
```
=== CHIPMUNK HOTS TEST ===

Testing basic HOTS functionality...
Setting up HOTS parameters...
✓ HOTS setup successful
Generating HOTS keys...
✓ HOTS key generation successful
Debug: pk.v0 first coeffs: 1938104 0 0 0
Debug: pk.v1 first coeffs: 342698 0 0 0
Signing test message...
✓ HOTS signing successful
Debug: signature[0] first coeffs: 270560 0 0 0
Verifying signature...
🔍 HOTS verify: Starting detailed verification...
```

### Текущий статус (проблема):
```
❌ VERIFICATION FAILED: Equations don't match
  Total differing coefficients: 256/256
💥 SOME HOTS TESTS FAILED! 💥
```

## 🐛 Отладка

### Детальный вывод математики:
Тест автоматически выводит пошаговую математическую отладку:
- H(m) коэффициенты (хеш сообщения)
- NTT преобразования
- Левая часть уравнения: Σ(a_i * σ_i)
- Правая часть уравнения: H(m) * v0 + v1
- Сравнение результатов

### Известные проблемы:

#### 1. H(m) генерирует все нули
```
H(m) first coeffs: 0 0 0 0  # Подозрительно!
```
**Возможная причина**: Ошибка в `chipmunk_poly_from_hash()`

#### 2. Математическое несоответствие
```
Left side first coeffs:  [разные числа]
Right side first coeffs: [другие числа]
```
**Возможная причина**: Ошибка в NTT операциях или арифметике

### Логи компиляции:
```bash
# Пересборка с отладкой
make chipmunk_hots_test 2>&1 | grep -E "(error|warning|undefined)"
```

## 📁 Структура файлов

### Исходные файлы:
```
dap-sdk/crypto/src/chipmunk/
├── chipmunk_hots.h              # API HOTS модуля
├── chipmunk_hots.c              # Реализация HOTS (336 строк)
├── chipmunk_poly.h              # API полиномов
├── chipmunk_poly.c              # Полиномные операции
├── chipmunk_progress.md         # Подробная документация
├── HOTS_BUILD_AND_TEST.md       # Этот файл
└── Chipmunk.orig/               # Оригинальный Rust код
    └── src/ots_sig/mod.rs       # Эталонная реализация
```

### Тестовые файлы:
```
dap-sdk/crypto/test/crypto/
├── chipmunk_hots_test.c         # HOTS тесты (212 строк)
├── CMakeLists.txt               # Конфигурация сборки
└── dap_test/                    # Тестовый фреймворк
```

### Сборочные файлы:
```
build-debug/
├── dap-sdk/crypto/test/crypto/
│   └── chipmunk_hots_test       # Скомпилированный тест
└── dap-sdk/crypto/libdap_crypto.a  # Криптобиблиотека
```

## 🔧 Технические детали

### Алгоритм HOTS:
1. **Setup**: Генерация GAMMA=6 случайных полиномов a[i]
2. **Keygen**: v0 = Σ(a_i * s0_i), v1 = Σ(a_i * s1_i)  
3. **Sign**: σ[i] = s0[i] * H(m) + s1[i]
4. **Verify**: Σ(a_i * σ_i) ?= H(m) * v0 + v1

### Параметры:
- `CHIPMUNK_Q = 3168257` (модуль)
- `CHIPMUNK_GAMMA = 6` (количество полиномов)
- `CHIPMUNK_N = 256` (степень полиномов)
- `CHIPMUNK_ALPHA_H = 37` (вес тернарного полинома)

### API функции:
```c
int chipmunk_hots_setup(chipmunk_hots_params_t *a_params);
int chipmunk_hots_keygen(const uint8_t a_seed[32], uint32_t a_counter, 
                        const chipmunk_hots_params_t *a_params,
                        chipmunk_hots_pk_t *a_pk, chipmunk_hots_sk_t *a_sk);
int chipmunk_hots_sign(const chipmunk_hots_sk_t *a_sk, const uint8_t *a_message, 
                      size_t a_message_len, chipmunk_hots_signature_t *a_signature);
int chipmunk_hots_verify(const chipmunk_hots_pk_t *a_pk, const uint8_t *a_message,
                        size_t a_message_len, const chipmunk_hots_signature_t *a_signature,
                        const chipmunk_hots_params_t *a_params);
```

## 🎯 Статус и следующие шаги

### ✅ Работает:
- ✅ Сборка проекта и тестов
- ✅ HOTS setup (генерация параметров)
- ✅ HOTS keygen (генерация ключей)
- ✅ HOTS sign (создание подписи)

### ❌ Не работает:
- ❌ HOTS verify (верификация подписи)

### 🔍 В отладке:
1. **Приоритет 1**: Исправить `chipmunk_poly_from_hash()` 
   - Убедиться что генерирует правильный тернарный полином
   - Проверить что не все коэффициенты нули

2. **Приоритет 2**: Валидировать NTT операции
   - Проверить `chipmunk_ntt()` / `chipmunk_invntt()`
   - Убедиться в корректности модульной арифметики

3. **Приоритет 3**: Сравнить с оригинальным Rust кодом
   - Пошаговое сравнение математики
   - Проверка промежуточных результатов

## 📞 Контакты и ресурсы

- **Проект**: Cellframe Node RC-6.0
- **Модуль**: DAP-SDK Crypto / Chipmunk
- **Оригинальный код**: `dap-sdk/crypto/src/chipmunk/Chipmunk.orig/`
- **Документация**: `dap-sdk/crypto/src/chipmunk/chipmunk_progress.md`

---

*Последнее обновление: 29.05.2025* 