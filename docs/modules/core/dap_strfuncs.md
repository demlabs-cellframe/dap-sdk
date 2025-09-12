# DAP String Functions (Функции работы со строками)

## Обзор

Модуль `dap_strfuncs` предоставляет обширный набор функций для работы со строками в DAP SDK. Включает стандартные операции, работу с UTF-8, валидацию, форматирование и специализированные функции для блокчейн приложений.

## Назначение

DAP SDK часто работает с текстовыми данными:
- **Парсинг конфигураций**: Чтение и валидация настроек
- **Криптовалютные операции**: Форматирование сумм и адресов
- **Сетевые протоколы**: Кодирование/декодирование строковых данных
- **Логирование**: Форматированный вывод отладочной информации
- **Валидация ввода**: Проверка корректности пользовательских данных

## Основные возможности

### 📝 **Базовые строковые операции**
- Копирование и дублирование строк
- Сравнение и поиск подстрок
- Конкатенация и разделение
- Замена символов

### 🔄 **Преобразования**
- Изменение регистра (upper/lower case)
- Реверс строк
- Удаление пробелов
- Преобразование чисел в строки и обратно

### 🌍 **UTF-8 поддержка**
- Конвертация UTF-16 ↔ UTF-8
- Работа с Unicode символами
- Безопасная обработка многобайтовых символов

### ✅ **Валидация**
- Проверка паролей
- Валидация алфавитно-цифровых строк
- Проверка корректности форматов

### 🔢 **Числовые преобразования**
- Преобразование строк в большие числа (128-bit, 256-bit)
- Форматированный вывод больших чисел
- Поддержка различных систем счисления

## API Функции

### Базовые операции

```c
// Длина строки
size_t dap_strlen(const char *a_str);

// Дублирование строки
char* dap_strdup(const char *a_str);

// Сравнение строк
int dap_strcmp(const char *a_str1, const char *a_str2);
int dap_strncmp(const char *a_str1, const char *a_str2, size_t a_n);

// Конкатенация строк
char* dap_strcat2(const char* s1, const char* s2);

// Копирование с ограничением
char *dap_strncpy(char *a_dst, const char *a_src, size_t a_limit);
char* dap_stpcpy(char *a_dest, const char *a_src);
```

### Расширенные операции

```c
// Поиск подстроки с учетом длины
char* dap_strstr_len(const char *a_haystack, ssize_t a_haystack_len, const char *a_needle);

// Поиск в массиве строк
const char* dap_str_find(const char **a_str_array, const char *a_str);

// Объединение массива строк с разделителем
char* dap_strjoinv(const char *a_separator, char **a_str_array);
char *dap_strjoin(const char *a_separator, ...);

// Разделение строки на токены
char** dap_strsplit(const char *a_string, const char *a_delimiter, int a_max_tokens);

// Подсчет символов в строке
size_t dap_str_symbol_count(const char *a_str, char a_sym);

// Форматированная печать
DAP_PRINTF_ATTR(1, 2) char *dap_strdup_printf(const char *a_format, ...);
char* dap_strdup_vprintf(const char *a_format, va_list a_args);
```

### Обработка пробелов

```c
// Удаление пробелов
char *dap_str_remove_spaces(char *a_str);

// Удаление ведущих пробелов
char* dap_strchug(char *a_string);

// Удаление завершающих пробелов
char* dap_strchomp(char *a_string);

// Удаление ведущих и завершающих пробелов
#define dap_strstrip(a_string) dap_strchomp(dap_strchug(a_string))
```

### Изменение регистра

```c
// Преобразование в верхний регистр
char* dap_strup(const char *a_str, ssize_t a_len);

// Преобразование в нижний регистр
char* dap_strdown(const char *a_str, ssize_t a_len);

// Реверс строки
char* dap_strreverse(char *a_string);
```

### Замена символов

```c
// Замена одного символа на другой
char *dap_str_replace_char(const char *a_src, char a_ch1, char a_ch2);
```

### Работа с массивами строк

```c
// Подсчет строк в массиве
size_t dap_str_countv(char **a_str_array);

// Добавление массива к массиву
char **dap_str_appv(char **a_dst, char **a_src, size_t *a_count);

// Копирование массива строк
char** dap_strdupv(const char **a_str_array);

// Освобождение массива строк
void dap_strfreev(char **a_str_array);
```

### Валидация

```c
// Проверка алфавитно-цифровых символов
bool dap_isstralnum(const char *c);

// Валидация пароля (латиница, цифры, специальные символы кроме пробела)
bool dap_check_valid_password(const char *a_str, size_t a_str_len);
```

## Числовые преобразования

### Преобразование строк в числа

```c
#ifdef DAP_GLOBAL_IS_INT128
// Преобразование строки в 128-битные числа
uint128_t dap_strtou128(const char *p, char **endp, int base);
int128_t dap_strtoi128(const char *p, char **endp, int base);

// Упрощенные версии (десятичная система)
static inline int128_t dap_atoi128(const char *p) {
    return dap_strtoi128(p, (char**)NULL, 10);
}

static inline uint128_t dap_atou128(const char *p) {
    return dap_strtou128(p, (char**)NULL, 10);
}
#endif
```

### Преобразование чисел в строки

```c
#ifdef DAP_GLOBAL_IS_INT128
// Преобразование 128-битных чисел в строки
char *dap_utoa128(char *dest, uint128_t v, int base);
char *dap_itoa128(char *a_str, int128_t a_value, int a_base);
#endif
```

## UTF-8 и Unicode поддержка

### Работа с Unicode

```c
// Типы для Unicode символов
typedef uint32_t unichar;    // Unicode character (UTF-32)
typedef uint16_t unichar2;   // UTF-16 character

// Конвертация Unicode символа в UTF-8
int dap_unichar_to_utf8(unichar c, char *outbuf);

// Конвертация UTF-16 строки в UTF-8
char* dap_utf16_to_utf8(const unichar2 *str, long len,
                       long *items_read, long *items_written);
```

## Использование

### Базовые операции со строками

```c
#include "dap_strfuncs.h"

// Дублирование строки
char *original = "Hello, World!";
char *copy = dap_strdup(original);

// Сравнение строк
if (dap_strcmp(str1, str2) == 0) {
    printf("Строки равны\n");
}

// Конкатенация
char *combined = dap_strcat2("Hello, ", "World!");
// Результат: "Hello, World!"

// Форматированная строка
char *formatted = dap_strdup_printf("Value: %d, String: %s", 42, "test");
// Результат: "Value: 42, String: test"
```

### Разделение и объединение строк

```c
// Разделение строки по разделителю
char *input = "apple,banana,cherry";
char **fruits = dap_strsplit(input, ",", -1); // -1 = все токены

// Результат:
// fruits[0] = "apple"
// fruits[1] = "banana"
// fruits[2] = "cherry"
// fruits[3] = NULL

// Объединение с разделителем
char *result = dap_strjoinv(" | ", fruits);
// Результат: "apple | banana | cherry"

// Освобождение памяти
dap_strfreev(fruits);
free(result);
```

### Работа с пробелами

```c
char *text = "   Hello, World!   ";

// Удаление всех пробелов
char *no_spaces = dap_str_remove_spaces(text);
// Результат: "Hello,World!"

// Удаление только ведущих пробелов
char *no_leading = dap_strchug(text);
// Результат: "Hello, World!   "

// Удаление только завершающих пробелов
char *no_trailing = dap_strchomp(text);
// Результат: "   Hello, World!"

// Удаление ведущих и завершающих пробелов
char *clean = dap_strstrip(text);
// Результат: "Hello, World!"
```

### Преобразование регистра

```c
char *text = "Hello, World!";

// В верхний регистр
char *upper = dap_strup(text, -1); // -1 = вся строка
// Результат: "HELLO, WORLD!"

// В нижний регистр
char *lower = dap_strdown(text, -1);
// Результат: "hello, world!"

// Реверс строки
char *reversed = dap_strreverse(dap_strdup(text));
// Результат: "!dlroW ,olleH"

free(upper);
free(lower);
free(reversed);
```

### Работа с большими числами

```c
#ifdef DAP_GLOBAL_IS_INT128
// Преобразование строки в 256-битное число
const char *big_num_str = "1234567890123456789012345678901234567890";
uint256_t big_num = GET_256_FROM_64(dap_atou128(big_num_str));

// Форматированный вывод
char buffer[128];
dap_utoa128(buffer, big_num.hi, 10); // Старшие 128 бит
printf("Big number: %s\n", buffer);
#endif
```

### Валидация данных

```c
// Валидация пароля
const char *password = "MySecurePass123!";
if (dap_check_valid_password(password, strlen(password))) {
    printf("Пароль валиден\n");
} else {
    printf("Пароль содержит недопустимые символы\n");
}

// Проверка алфавитно-цифровых символов
const char *input = "User123";
if (dap_isstralnum(input)) {
    printf("Строка содержит только буквы и цифры\n");
}
```

### Работа с массивами строк

```c
// Создание массива строк
char *array1[] = {"apple", "banana", NULL};
char *array2[] = {"cherry", "date", NULL};
size_t count = 0;

// Объединение массивов
char **combined = dap_str_appv(array1, array2, &count);
// Результат: ["apple", "banana", "cherry", "date", NULL]

// Подсчет строк
size_t total_count = dap_str_countv(combined);
// Результат: 4

// Копирование массива
char **copy = dap_strdupv((const char **)combined);

// Освобождение памяти
dap_strfreev(combined);
dap_strfreev(copy);
```

## Особенности реализации

### Управление памятью

- **Автоматическое выделение**: Большинство функций выделяют память динамически
- **Обязательное освобождение**: Память должна освобождаться вызывающим кодом
- **NULL-terminated**: Все строки завершаются нулевым символом
- **Безопасность**: Защита от переполнения буферов

### Производительность

- **Оптимизированные алгоритмы**: Эффективные реализации для больших строк
- **Минимальные копирования**: Использование указателей где возможно
- **Векторизация**: Поддержка SIMD инструкций для массовых операций

### Кроссплатформенность

- **Windows поддержка**: Специфические реализации для Win32 API
- **POSIX совместимость**: Полная поддержка Unix-like систем
- **Автоматическая адаптация**: Выбор оптимальной реализации для платформы

## Использование в DAP SDK

### Конфигурационные файлы

```c
// Парсинг конфигурационного файла
char *config_line = "host=localhost;port=8080;timeout=30";
char **parts = dap_strsplit(config_line, ";", -1);

for (int i = 0; parts[i]; i++) {
    char **key_value = dap_strsplit(parts[i], "=", 2);
    if (key_value && key_value[0] && key_value[1]) {
        printf("Config: %s = %s\n", key_value[0], key_value[1]);
    }
    dap_strfreev(key_value);
}
dap_strfreev(parts);
```

### Форматирование криптовалютных сумм

```c
// Форматирование суммы токенов
uint256_t token_amount = GET_256_FROM_64(1234567890123456789ULL); // 1.234567890123456789 токенов
char *formatted = dap_uint256_decimal_to_char(token_amount);
printf("Balance: %s tokens\n", formatted);
free(formatted);
```

### Валидация пользовательского ввода

```c
// Валидация адреса кошелька
const char *wallet_address = user_input;
if (dap_strlen(wallet_address) >= 32 &&
    dap_isstralnum(wallet_address)) {
    // Адрес валиден для дальнейшей обработки
    process_wallet_address(wallet_address);
} else {
    printf("Некорректный формат адреса\n");
}
```

### Логирование и отладка

```c
// Форматированное логирование
uint64_t transaction_id = 12345;
const char *status = "completed";
char *log_message = dap_strdup_printf(
    "Transaction %llu %s at %s",
    transaction_id, status, get_timestamp()
);
log_info(log_message);
free(log_message);
```

## Связанные модули

- `dap_math_ops.h` - Математические операции с большими числами
- `dap_math_convert.h` - Преобразование чисел в строки и обратно
- `dap_common.h` - Общие определения и макросы
- `dap_fnmatch.h` - Работа с шаблонами имен файлов

## Замечания по безопасности

### Управление памятью
```c
// Правильное освобождение памяти
char *result = dap_strdup_printf("Value: %d", 42);
// ... использование result ...
free(result); // Обязательно освободить!

// Правильная работа с массивами
char **array = dap_strsplit(input, ",", -1);
// ... использование array ...
dap_strfreev(array); // Освободить весь массив
```

### Валидация ввода
```c
// Проверка границ перед операциями
if (input && dap_strlen(input) < MAX_INPUT_SIZE) {
    char *processed = dap_strstrip(input);
    // ... безопасная обработка ...
    free(processed);
} else {
    printf("Входные данные слишком длинные\n");
}
```

### Unicode безопасность
```c
// Безопасная работа с UTF-8
char utf8_buffer[6]; // Максимум 6 байт на символ
int bytes_written = dap_unichar_to_utf8(unicode_char, utf8_buffer);
if (bytes_written > 0) {
    utf8_buffer[bytes_written] = '\0';
    // ... использование UTF-8 строки ...
}
```

## Отладка

### Проверка корректности строк
```c
// Функция для отладочного вывода строки
void debug_string(const char *str, const char *name) {
    if (!str) {
        printf("%s: NULL\n", name);
        return;
    }
    printf("%s: \"%s\" (length: %zu)\n", name, str, dap_strlen(str));
}

// Проверка массива строк
void debug_string_array(char **array, const char *name) {
    printf("%s:\n", name);
    for (size_t i = 0; array[i]; i++) {
        printf("  [%zu]: \"%s\"\n", i, array[i]);
    }
}
```

### Тестирование функций
```c
// Комплексный тест строковых функций
void test_string_functions() {
    // Тест базовых операций
    char *original = "Hello";
    char *dup = dap_strdup(original);
    assert(dap_strcmp(original, dup) == 0);

    // Тест преобразований регистра
    char *upper = dap_strup(original, -1);
    assert(dap_strcmp(upper, "HELLO") == 0);

    // Тест разделения
    char *csv = "a,b,c";
    char **parts = dap_strsplit(csv, ",", -1);
    assert(dap_str_countv(parts) == 3);

    // Очистка
    free(dup);
    free(upper);
    dap_strfreev(parts);

    printf("Все тесты строковых функций пройдены!\n");
}
```

Этот модуль предоставляет фундаментальные возможности для работы со строками в DAP SDK, обеспечивая безопасность, производительность и кроссплатформенность всех текстовых операций.

