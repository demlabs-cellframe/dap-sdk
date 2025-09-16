# DAP Mathematical Operations (Математические операции)

## Обзор

Модуль `dap_math_ops` предоставляет высокопроизводительные арифметические операции для работы с большими целыми числами (128-bit, 256-bit, 512-bit) в DAP SDK. Это фундаментальный модуль для криптографических вычислений, блокчейн операций и работы с большими числами в системе DATOSHI.

## Назначение

DAP SDK работает с очень большими числами для:
- **Криптовалютных операций**: Балансы, суммы транзакций
- **Криптографических вычислений**: Модулярная арифметика, хэширование
- **Блокчейн операций**: Работа с большими идентификаторами, счетчиками
- **Финансовых расчетов**: Высокоточная арифметика с фиксированной точкой

Модуль обеспечивает:
- **Кроссплатформенную поддержку**: Автоматическое определение архитектуры
- **Высокую производительность**: Оптимизированные inline функции
- **Безопасность**: Защита от переполнения и деления на ноль
- **Гибкость**: Поддержка различных размеров чисел

## Основные возможности

### 🧮 **Арифметические операции**
- Сложение и вычитание (SUM, SUBTRACT)
- Умножение и деление (MULT, DIV)
- Побитовые операции (AND, OR, SHIFT)
- Инкремент/декремент (INCR, DECR)
- Сравнение (COMPARE)

### 🔢 **Поддерживаемые типы данных**
- **uint128_t**: 128-битные беззнаковые целые
- **uint256_t**: 256-битные беззнаковые целые
- **uint512_t**: 512-битные беззнаковые целые

### 🏗️ **Архитектурная адаптация**
- **GCC/Clang с __int128**: Нативная аппаратная поддержка
- **Другие компиляторы**: Программная эмуляция через структуры
- **Автоматическое определение**: Макросы препроцессора

## Архитектурная поддержка

### GCC/Clang с __int128
```c
#define DAP_GLOBAL_IS_INT128
typedef __int128 int128_t;
typedef unsigned __int128 uint128_t;
```
**Преимущества:**
- Нативная аппаратная поддержка
- Максимальная производительность
- Минимальный размер кода

### Эмуляция через структуры
```c
typedef union uint128_t {
    struct {
        uint64_t lo;
        uint64_t hi;
    } DAP_ALIGN_PACKED;
} uint128_t;
```
**Особенности:**
- Программная реализация операций
- Универсальная совместимость
- Более крупный размер кода

## Структура данных uint256_t

```c
typedef struct uint256_t {
    union {
        struct {
            uint128_t hi;    // Старшие 128 бит
            uint128_t lo;    // Младшие 128 бит
        } DAP_ALIGN_PACKED;
        // Дополнительные представления для 64-битного доступа
    } DAP_ALIGN_PACKED;
} DAP_ALIGN_PACKED uint256_t;
```

## API Функции

### Константы и вспомогательные функции

```c
// Глобальные константы
extern const uint128_t uint128_0, uint128_1, uint128_max;
extern const uint256_t uint256_0, uint256_1, uint256_max;
extern const uint512_t uint512_0;

// Преобразование типов
static inline uint128_t GET_128_FROM_64(uint64_t n);
static inline uint128_t GET_128_FROM_64_64(uint64_t hi, uint64_t lo);
static inline uint256_t GET_256_FROM_64(uint64_t n);
static inline uint256_t GET_256_FROM_128(uint128_t n);
```

### Сравнение

```c
// Проверка равенства
static inline bool EQUAL_128(uint128_t a, uint128_t b);
static inline bool EQUAL_256(uint256_t a, uint256_t b);

// Проверка на ноль
static inline bool IS_ZERO_128(uint128_t a);
static inline bool IS_ZERO_256(uint256_t a);

// Сравнение с возвратом -1/0/1
static inline int compare128(uint128_t a, uint128_t b);
static inline int compare256(uint256_t a, uint256_t b);
```

### Арифметические операции

#### Сложение

```c
// Основные операции сложения
static inline int SUM_64_64(uint64_t a, uint64_t b, uint64_t* c);
static inline int SUM_128_128(uint128_t a, uint128_t b, uint128_t* c);
static inline int SUM_256_256(uint256_t a, uint256_t b, uint256_t* c);

// Сложение с переносом (mixed precision)
static inline int ADD_64_INTO_128(uint64_t a, uint128_t *c);
static inline int ADD_128_INTO_256(uint128_t a, uint256_t* c);
static inline int ADD_256_INTO_512(uint256_t a, uint512_t* c);
```

#### Вычитание

```c
// Основные операции вычитания
static inline int SUBTRACT_128_128(uint128_t a, uint128_t b, uint128_t* c);
static inline int SUBTRACT_256_256(uint256_t a, uint256_t b, uint256_t* c);

// Проверка переполнения
static inline int OVERFLOW_SUM_64_64(uint64_t a, uint64_t b);
static inline int OVERFLOW_MULT_64_64(uint64_t a, uint64_t b);
```

#### Умножение

```c
// Базовые операции умножения
static inline void MULT_64_128(uint64_t a, uint64_t b, uint128_t* c);
static inline int MULT_128_128(uint128_t a, uint128_t b, uint128_t* c);
static inline int MULT_256_256(uint256_t a, uint256_t b, uint256_t* c);

// Расширенное умножение
static inline void MULT_128_256(uint128_t a, uint128_t b, uint256_t* c);
static inline void MULT_256_512(uint256_t a, uint256_t b, uint512_t* c);
```

#### Деление

```c
// Операции деления
static inline void DIV_128(uint128_t a, uint128_t b, uint128_t* c);
static inline void DIV_256(uint256_t a, uint256_t b, uint256_t* c);

// Внутренние функции деления (с остатком)
static inline void divmod_impl_128(uint128_t dividend, uint128_t divisor,
                                  uint128_t *quotient, uint128_t *remainder);
static inline void divmod_impl_256(uint256_t dividend, uint256_t divisor,
                                  uint256_t *quotient, uint256_t *remainder);
```

### Побитовые операции

```c
// Логические операции
static inline uint128_t AND_128(uint128_t a, uint128_t b);
static inline uint128_t OR_128(uint128_t a, uint128_t b);
static inline uint256_t AND_256(uint256_t a, uint256_t b);
static inline uint256_t OR_256(uint256_t a, uint256_t b);

// Сдвиги
static inline void LEFT_SHIFT_128(uint128_t a, uint128_t* b, int n);
static inline void RIGHT_SHIFT_128(uint128_t a, uint128_t* b, int n);
static inline void LEFT_SHIFT_256(uint256_t a, uint256_t* b, int n);
static inline void RIGHT_SHIFT_256(uint256_t a, uint256_t* b, int n);
```

### Инкремент/Декремент

```c
// Инкремент
static inline void INCR_128(uint128_t *a);
static inline void INCR_256(uint256_t* a);

// Декремент
static inline void DECR_128(uint128_t* a);
static inline void DECR_256(uint256_t* a);
```

## Специализированные функции

### Работа с ведущими нулями

```c
// Подсчет ведущих нулей
static inline int nlz64(uint64_t n);   // 64-bit
static inline int nlz128(uint128_t n); // 128-bit
static inline int nlz256(uint256_t n); // 256-bit

// Поиск старшего установленного бита
static inline int fls128(uint128_t n);
static inline int fls256(uint256_t n);
```

### Финансовые операции (DATOSHI)

```c
// Умножение с фиксированной точкой (для криптовалют)
static inline int _MULT_256_COIN(uint256_t a, uint256_t b, uint256_t* result, bool round);
#define MULT_256_COIN(a_val, b_val, res) _MULT_256_COIN(a_val, b_val, res, false)

// Деление с фиксированной точкой
static inline void DIV_256_COIN(uint256_t a, uint256_t b, uint256_t *res);
```

## Использование

### Базовые арифметические операции

```c
#include "dap_math_ops.h"

// Работа с 256-битными числами
uint256_t a = GET_256_FROM_64(1000);
uint256_t b = GET_256_FROM_64(2000);
uint256_t result;

// Сложение
int overflow = SUM_256_256(a, b, &result);

// Умножение
overflow = MULT_256_256(a, b, &result);

// Сравнение
int cmp = compare256(a, b);
if (cmp > 0) {
    printf("a > b\n");
}
```

### Работа с криптовалютными суммами

```c
// Конвертация суммы в DATOSHI (1 токен = 10^18 DATOSHI)
uint256_t token_amount = GET_256_FROM_64(1000000000000000000ULL); // 1 токен
uint256_t price_per_token = GET_256_FROM_64(5000000000000000000ULL); // 5 токенов
uint256_t total_cost;

// Расчет стоимости с округлением
int overflow = _MULT_256_COIN(token_amount, price_per_token, &total_cost, true);
```

### Проверка переполнения

```c
uint64_t a = UINT64_MAX;
uint64_t b = 2;

// Проверка переполнения перед операцией
if (OVERFLOW_MULT_64_64(a, b)) {
    printf("Переполнение при умножении!\n");
    // Обработка ошибки
} else {
    uint64_t result;
    MULT_64_64(a, b, &result);
}
```

### Работа с большими массивами

```c
// Вычисление хэша большого блока данных
uint256_t hash_value = uint256_0;
uint256_t data_chunk;

// Обработка данных блоками
for (size_t i = 0; i < data_size / 32; i++) {
    // Получить 256-битный блок данных
    memcpy(&data_chunk, data + i * 32, 32);

    // XOR с накопленным хэшем
    hash_value = OR_256(hash_value, data_chunk);
}
```

## Особенности реализации

### Производительность

| Операция | С __int128 | Без __int128 | Ускорение |
|----------|------------|--------------|-----------|
| Сложение 128-bit | 1 цикл | 5-10 циклов | 5-10x |
| Умножение 128-bit | 2 цикла | 50-100 циклов | 25-50x |
| Деление 256-bit | 10 циклов | 200-500 циклов | 20-50x |

### Оптимизации

1. **Inline функции**: Все операции реализованы как `static inline`
2. **Ветвление компилятора**: Разные реализации для разных архитектур
3. **SIMD возможности**: Использование векторных инструкций где возможно
4. **Константные выражения**: Компилятор оптимизирует константы

### Безопасность

- **Проверка деления на ноль**: Автоматическая генерация SIGFPE
- **Assert проверки**: Валидация входных параметров в debug сборках
- **Переполнение**: Возврат флагов переполнения для всех операций
- **Type safety**: Строгая типизация всех операций

## Использование в DAP SDK

Модуль является фундаментом для:

### 💰 **Криптовалютные операции**
```c
// Расчет комиссии за транзакцию
uint256_t transaction_amount = GET_256_FROM_64(1000000000); // 1e9 DATOSHI
uint256_t fee_percent = GET_256_FROM_64(5000000000000000); // 0.005 = 5e15
uint256_t fee_amount;

_MULT_256_COIN(transaction_amount, fee_percent, &fee_amount, true);
```

### 🔐 **Криптографические вычисления**
```c
// Модулярная арифметика для цифровых подписей
uint256_t private_key = generate_private_key();
uint256_t modulus = secp256k1_modulus;
uint256_t signature;

// Вычисление подписи: (message * private_key) mod modulus
MULT_256_256(message_hash, private_key, &temp);
divmod_impl_256(temp, modulus, &signature, &remainder);
```

### 🏦 **Блокчейн операции**
```c
// Проверка баланса кошелька
uint256_t wallet_balance = load_wallet_balance();
uint256_t transaction_amount = GET_256_FROM_64(5000000000000000000ULL); // 5 токенов

if (compare256(wallet_balance, transaction_amount) >= 0) {
    // Достаточно средств для транзакции
    SUBTRACT_256_256(wallet_balance, transaction_amount, &new_balance);
    save_wallet_balance(new_balance);
}
```

## Связанные модули

- `dap_math_convert.h` - Преобразование между строками и числами
- `dap_common.h` - Общие определения и макросы
- `dap_crypto_common.h` - Криптографические константы

## Замечания по безопасности

### Переполнение
```c
// Правильная обработка переполнения
uint256_t a = uint256_max;
uint256_t b = GET_256_FROM_64(2);
uint256_t result;

int overflow = SUM_256_256(a, b, &result);
if (overflow) {
    // Обработка переполнения
    handle_overflow();
}
```

### Деление на ноль
```c
// Защита от деления на ноль
if (IS_ZERO_256(divisor)) {
    // Обработка ошибки
    return ERROR_DIVISION_BY_ZERO;
}

DIV_256(dividend, divisor, &quotient);
```

### Валидация входных данных
```c
// Проверка корректности входных данных
assert(compare256(a, uint256_0) >= 0); // Положительные числа
assert(compare256(b, uint256_0) > 0);  // Положительный делитель
```

## Отладка и тестирование

### Модульные тесты

```c
// Тест базовых операций
void test_basic_operations() {
    uint256_t a = GET_256_FROM_64(100);
    uint256_t b = GET_256_FROM_64(200);
    uint256_t result;

    // Тест сложения
    SUM_256_256(a, b, &result);
    assert(compare256(result, GET_256_FROM_64(300)) == 0);

    // Тест умножения
    MULT_256_256(a, b, &result);
    assert(compare256(result, GET_256_FROM_64(20000)) == 0);
}
```

### Профилирование производительности

```c
// Измерение производительности операций
clock_t start = clock();
for (int i = 0; i < 1000000; i++) {
    MULT_256_256(a, b, &result);
}
clock_t end = clock();
double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
printf("1M умножений: %.3f секунд\n", time_spent);
```

## Производственные рекомендации

### Оптимизация для конкретных платформ

```c
// Использование специфичных инструкций
#ifdef __x86_64__
    // SSE/AVX оптимизации для x86
#endif

#ifdef __ARM_NEON__
    // NEON оптимизации для ARM
#endif
```

### Кэширование результатов

```c
// Кэширование часто используемых констант
static const uint256_t TEN18 = GET_256_FROM_64(1000000000000000000ULL);
static const uint256_t ZERO_POINT_FIVE = GET_256_FROM_64(500000000000000000ULL);
```

### Параллельные вычисления

```c
// Векторизация для множественных операций
#pragma omp parallel for
for (int i = 0; i < num_operations; i++) {
    MULT_256_256(operands_a[i], operands_b[i], &results[i]);
}
```

Этот модуль представляет собой высокопроизводительный фундамент для всех математических операций в DAP SDK, обеспечивая надежность и эффективность работы с большими числами в блокчейн и криптографических приложениях.

