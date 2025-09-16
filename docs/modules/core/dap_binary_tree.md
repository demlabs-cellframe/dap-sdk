# dap_binary_tree.h/c - Система бинарных деревьев поиска

## Обзор

Модуль `dap_binary_tree` предоставляет реализацию бинарных деревьев поиска для DAP SDK. Это фундаментальная структура данных, обеспечивающая эффективные операции поиска, вставки и удаления элементов с логарифмической сложностью.

## Основные возможности

- **Бинарное дерево поиска**: Эффективная структура для хранения упорядоченных данных
- **Строковые ключи**: Поддержка строковых ключей с лексикографическим сравнением
- **Автоматическая балансировка**: Отсутствует (стандартное бинарное дерево)
- **Интеграция с dap_list**: Преобразование дерева в связный список
- **Рекурсивные алгоритмы**: Все операции реализованы рекурсивно
- **Безопасность памяти**: Автоматическое управление памятью

## Структура данных

### dap_binary_tree_t

Основная структура узла бинарного дерева:

```c
typedef struct dap_binary_tree {
    dap_binary_tree_key_t key;           // Ключ (строка)
    void *data;                          // Пользовательские данные
    struct dap_binary_tree *left;        // Левый потомок
    struct dap_binary_tree *right;       // Правый потомок
} dap_binary_tree_t;
```

**Поля:**
- `key` - строковый ключ для поиска и упорядочивания
- `data` - указатель на пользовательские данные (void*)
- `left` - указатель на левое поддерево
- `right` - указатель на правое поддерево

### Типы ключей

```c
typedef const char *dap_binary_tree_key_t;
```

**Характеристики:**
- Строковый тип данных
- Неизменяемый (const char*)
- Лексикографическое сравнение

### Макросы сравнения

```c
#define KEY_LS(a, b) (strcmp(a, b) < 0)    // a < b
#define KEY_GT(a, b) (strcmp(a, b) > 0)    // a > b
#define KEY_EQ(a, b) (strcmp(a, b) == 0)   // a == b
```

**Особенности:**
- Используют стандартную функцию `strcmp()`
- Возвращают логические значения
- Оптимизированы для строковых операций

## API Reference

### Создание и модификация дерева

#### dap_binary_tree_insert()

```c
dap_binary_tree_t *dap_binary_tree_insert(
    dap_binary_tree_t *a_tree_root,
    dap_binary_tree_key_t a_key,
    void *a_data
);
```

**Описание**: Вставляет новый элемент в бинарное дерево поиска.

**Параметры:**
- `a_tree_root` - корень дерева (может быть NULL для пустого дерева)
- `a_key` - строковый ключ для вставки
- `a_data` - пользовательские данные

**Возвращает:** Новый корень дерева или NULL при ошибке выделения памяти.

**Пример:**
```c
#include "dap_binary_tree.h"

int main() {
    dap_binary_tree_t *tree = NULL;

    // Вставка элементов
    tree = dap_binary_tree_insert(tree, "banana", (void*)1);
    tree = dap_binary_tree_insert(tree, "apple", (void*)2);
    tree = dap_binary_tree_insert(tree, "cherry", (void*)3);

    // Теперь дерево содержит: apple(2), banana(1), cherry(3)
    // в отсортированном порядке

    return 0;
}
```

### Поиск элементов

#### dap_binary_tree_search()

```c
void *dap_binary_tree_search(
    dap_binary_tree_t *a_tree_root,
    dap_binary_tree_key_t a_key
);
```

**Описание**: Ищет элемент по ключу в бинарном дереве.

**Параметры:**
- `a_tree_root` - корень дерева
- `a_key` - ключ для поиска

**Возвращает:** Указатель на данные или NULL, если ключ не найден.

**Пример:**
```c
// Поиск элемента
void *data = dap_binary_tree_search(tree, "banana");
if (data) {
    printf("Found: %d\n", (int)(uintptr_t)data);
} else {
    printf("Key not found\n");
}
```

### Минимум и максимум

#### dap_binary_tree_minimum()

```c
void *dap_binary_tree_minimum(dap_binary_tree_t *a_tree_root);
```

**Описание**: Находит минимальный элемент (самый левый лист).

**Возвращает:** Данные минимального элемента или NULL для пустого дерева.

#### dap_binary_tree_maximum()

```c
void *dap_binary_tree_maximum(dap_binary_tree_t *a_tree_root);
```

**Описание**: Находит максимальный элемент (самый правый лист).

**Возвращает:** Данные максимального элемента или NULL для пустого дерева.

**Пример:**
```c
// Поиск минимума и максимума
void *min_data = dap_binary_tree_minimum(tree);
void *max_data = dap_binary_tree_maximum(tree);

printf("Min: %d, Max: %d\n",
       (int)(uintptr_t)min_data,
       (int)(uintptr_t)max_data);
```

### Удаление элементов

#### dap_binary_tree_delete()

```c
dap_binary_tree_t *dap_binary_tree_delete(
    dap_binary_tree_t *a_tree_root,
    dap_binary_tree_key_t a_key
);
```

**Описание**: Удаляет элемент с указанным ключом из дерева.

**Параметры:**
- `a_tree_root` - корень дерева
- `a_key` - ключ удаляемого элемента

**Возвращает:** Новый корень дерева (может измениться после удаления).

**Пример:**
```c
// Удаление элемента
tree = dap_binary_tree_delete(tree, "banana");
printf("Element removed\n");
```

### Информация о дереве

#### dap_binary_tree_count()

```c
size_t dap_binary_tree_count(dap_binary_tree_t *a_tree_root);
```

**Описание**: Подсчитывает количество элементов в дереве.

**Возвращает:** Количество узлов в дереве.

**Пример:**
```c
size_t count = dap_binary_tree_count(tree);
printf("Tree contains %zu elements\n", count);
```

### Преобразование в список

#### dap_binary_tree_inorder_list()

```c
dap_list_t *dap_binary_tree_inorder_list(dap_binary_tree_t *a_tree_root);
```

**Описание**: Преобразует бинарное дерево в связный список в порядке in-order (левый-корень-правый).

**Возвращает:** Указатель на связный список или NULL для пустого дерева.

**Пример:**
```c
// Преобразование дерева в список
dap_list_t *list = dap_binary_tree_inorder_list(tree);

// Вывод элементов в отсортированном порядке
dap_list_t *current = list;
while (current) {
    printf("Key: %s, Data: %d\n",
           current->data,  // Здесь нужно хранить ключ отдельно
           (int)(uintptr_t)current->data);
    current = current->next;
}

// Освобождение списка
dap_list_free(list);
```

### Очистка дерева

#### dap_binary_tree_clear()

```c
void dap_binary_tree_clear(dap_binary_tree_t *a_tree_root);
```

**Описание**: Рекурсивно освобождает память, занятую деревом.

**Примечание:** Освобождает только структуру дерева, но не пользовательские данные!

**Пример:**
```c
// Очистка дерева (данные нужно освобождать отдельно)
dap_binary_tree_clear(tree);
tree = NULL;
```

## Алгоритмы и сложность

### Временная сложность операций

| Операция | Средняя сложность | Худший случай | Примечание |
|----------|------------------|---------------|------------|
| `dap_binary_tree_search` | O(log n) | O(n) | Зависит от сбалансированности |
| `dap_binary_tree_insert` | O(log n) | O(n) | То же самое |
| `dap_binary_tree_delete` | O(log n) | O(n) | То же самое |
| `dap_binary_tree_minimum` | O(log n) | O(n) | Проход к левому листу |
| `dap_binary_tree_maximum` | O(log n) | O(n) | Проход к правому листу |
| `dap_binary_tree_count` | O(n) | O(n) | Рекурсивный обход |
| `dap_binary_tree_inorder_list` | O(n) | O(n) | Линейный обход |

### Пространственная сложность

- **Каждый узел**: `sizeof(dap_binary_tree_t)` + длина ключа
- **Общая**: O(n) для n элементов
- **Рекурсия**: O(h) стек для высоты дерева h

## Примеры использования

### Пример 1: Телефонная книга

```c
#include "dap_binary_tree.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct contact {
    char *name;
    char *phone;
} contact_t;

contact_t *create_contact(const char *name, const char *phone) {
    contact_t *contact = malloc(sizeof(contact_t));
    contact->name = strdup(name);
    contact->phone = strdup(phone);
    return contact;
}

void destroy_contact(void *data) {
    contact_t *contact = (contact_t*)data;
    free(contact->name);
    free(contact->phone);
    free(contact);
}

int main() {
    dap_binary_tree_t *phonebook = NULL;

    // Добавление контактов
    phonebook = dap_binary_tree_insert(phonebook, "Alice", create_contact("Alice", "123-456"));
    phonebook = dap_binary_tree_insert(phonebook, "Bob", create_contact("Bob", "789-012"));
    phonebook = dap_binary_tree_insert(phonebook, "Charlie", create_contact("Charlie", "345-678"));

    // Поиск контакта
    contact_t *alice = (contact_t*)dap_binary_tree_search(phonebook, "Alice");
    if (alice) {
        printf("Alice's phone: %s\n", alice->phone);
    }

    // Получение отсортированного списка
    dap_list_t *sorted_list = dap_binary_tree_inorder_list(phonebook);
    printf("Phonebook (sorted by name):\n");

    dap_list_t *current = sorted_list;
    while (current) {
        contact_t *contact = (contact_t*)current->data;
        printf("  %s: %s\n", contact->name, contact->phone);
        current = current->next;
    }

    // Очистка
    dap_list_free(sorted_list);
    dap_binary_tree_clear(phonebook); // Только структура дерева

    // Отдельная очистка данных контактов
    // (нужно реализовать отдельный обход для освобождения данных)

    return 0;
}
```

### Пример 2: Кэш с ключами

```c
#include "dap_binary_tree.h"
#include <stdio.h>

#define CACHE_SIZE 100

typedef struct cache_entry {
    char *key;
    void *value;
    time_t timestamp;
} cache_entry_t;

dap_binary_tree_t *cache = NULL;

void cache_put(const char *key, void *value) {
    // Проверка размера кэша
    if (dap_binary_tree_count(cache) >= CACHE_SIZE) {
        // Удаление самого старого элемента
        // (нужна дополнительная логика для отслеживания времени)
    }

    cache_entry_t *entry = malloc(sizeof(cache_entry_t));
    entry->key = strdup(key);
    entry->value = value;
    entry->timestamp = time(NULL);

    cache = dap_binary_tree_insert(cache, key, entry);
}

void *cache_get(const char *key) {
    cache_entry_t *entry = (cache_entry_t*)dap_binary_tree_search(cache, key);
    if (entry) {
        entry->timestamp = time(NULL); // Обновление времени доступа
        return entry->value;
    }
    return NULL;
}

void cache_clear() {
    // Освобождение всех записей кэша
    if (cache) {
        // Рекурсивная очистка данных
        dap_binary_tree_clear(cache);
        cache = NULL;
    }
}
```

### Пример 3: Символьная таблица компилятора

```c
#include "dap_binary_tree.h"
#include <stdio.h>

typedef struct symbol {
    char *name;
    char *type;
    int scope_level;
    void *value;
} symbol_t;

dap_binary_tree_t *symbol_table = NULL;

void add_symbol(const char *name, const char *type, int scope) {
    symbol_t *sym = malloc(sizeof(symbol_t));
    sym->name = strdup(name);
    sym->type = strdup(type);
    sym->scope_level = scope;
    sym->value = NULL;

    symbol_table = dap_binary_tree_insert(symbol_table, name, sym);
    printf("Added symbol: %s (%s) at scope %d\n", name, type, scope);
}

symbol_t *lookup_symbol(const char *name) {
    return (symbol_t*)dap_binary_tree_search(symbol_table, name);
}

void print_symbol_table() {
    printf("Symbol table contents:\n");

    dap_list_t *symbols = dap_binary_tree_inorder_list(symbol_table);
    dap_list_t *current = symbols;

    while (current) {
        symbol_t *sym = (symbol_t*)current->data;
        printf("  %s: %s (scope %d)\n",
               sym->name, sym->type, sym->scope_level);
        current = current->next;
    }

    dap_list_free(symbols);
}

int main() {
    // Добавление символов
    add_symbol("x", "int", 0);
    add_symbol("main", "function", 0);
    add_symbol("printf", "function", 0);
    add_symbol("y", "float", 1);

    // Поиск символа
    symbol_t *sym = lookup_symbol("x");
    if (sym) {
        printf("Found symbol 'x': type=%s, scope=%d\n",
               sym->type, sym->scope_level);
    }

    // Вывод всей таблицы
    print_symbol_table();

    // Очистка (упрощенная версия)
    dap_binary_tree_clear(symbol_table);

    return 0;
}
```

## Свойства и ограничения

### Свойства бинарного дерева поиска

1. **Упорядоченность**: Все ключи в левом поддереве меньше корня, в правом - больше
2. **Уникальность**: Ключи должны быть уникальными
3. **Лексикографический порядок**: Строки сравниваются по алфавиту
4. **Рекурсивная структура**: Дерево состоит из поддеревьев

### Ограничения

1. **Не сбалансировано**: В худшем случае вырождается в связный список
2. **Только строковые ключи**: Нет поддержки других типов ключей
3. **Рекурсивные алгоритмы**: Ограничение по глубине стека
4. **Нет итераторов**: Нет встроенной поддержки итерации
5. **Память**: Каждый узел содержит дополнительные указатели

### Производительность

#### Преимущества:
- Быстрый поиск для сбалансированных деревьев
- Простая реализация
- Эффективное использование памяти
- Поддержка упорядоченного обхода

#### Недостатки:
- Не гарантирована сбалансированность
- Риск вырождения в список
- Рекурсия может вызвать переполнение стека
- Нет автоматической ребалансировки

## Сравнение с другими структурами

### Бинарное дерево vs Хеш-таблица

| Критерий | Бинарное дерево | Хеш-таблица |
|----------|----------------|-------------|
| Поиск | O(log n) - O(n) | O(1) среднее |
| Вставка | O(log n) - O(n) | O(1) среднее |
| Удаление | O(log n) - O(n) | O(1) среднее |
| Упорядоченность | ✅ Поддерживается | ❌ Не поддерживается |
| Память | O(n) | O(n) + overhead |
| Предсказуемость | ❌ Зависит от данных | ✅ Гарантированная |

### Бинарное дерево vs Красно-черное дерево

| Критерий | Бинарное дерево | Красно-черное дерево |
|----------|----------------|---------------------|
| Балансировка | ❌ Не гарантирована | ✅ Автоматическая |
| Сложность | Простая | Сложная |
| Гарантии | Нет | O(log n) для всех операций |
| Память | Меньше | Больше (цвет узлов) |
| Реализация | Простая | Сложная |

## Безопасность

### Рекомендации по безопасному использованию

```c
// ✅ Правильная обработка ошибок
dap_binary_tree_t *safe_tree_operations() {
    dap_binary_tree_t *tree = NULL;

    // Проверка перед вставкой
    if (!some_key || !some_data) {
        return NULL;
    }

    tree = dap_binary_tree_insert(tree, some_key, some_data);
    if (!tree) {
        fprintf(stderr, "Failed to insert element\n");
        return NULL;
    }

    return tree;
}

// ❌ Уязвимый код
void unsafe_tree_usage() {
    dap_binary_tree_t *tree = NULL;

    // Нет проверки входных данных
    tree = dap_binary_tree_insert(tree, NULL, NULL); // Может вызвать crash

    // Нет проверки результата
    dap_binary_tree_search(tree, invalid_key); // Небезопасно
}
```

### Управление памятью

```c
// Правильная очистка дерева с пользовательскими данными
void tree_clear_with_data(dap_binary_tree_t *tree,
                         void (*destroy_data)(void*)) {
    if (!tree) return;

    // Рекурсивная очистка
    tree_clear_with_data(tree->left, destroy_data);
    tree_clear_with_data(tree->right, destroy_data);

    // Освобождение данных
    if (destroy_data && tree->data) {
        destroy_data(tree->data);
    }

    // Освобождение узла
    DAP_FREE(tree);
}

// Использование
void destroy_contact(void *data) {
    contact_t *contact = (contact_t*)data;
    free(contact->name);
    free(contact->phone);
    free(contact);
}

// Очистка
tree_clear_with_data(tree, destroy_contact);
```

## Лучшие практики

### 1. Выбор структуры данных

```c
// Используйте бинарное дерево когда:
if (need_ordered_traversal || need_range_queries) {
    // Бинарное дерево подходит
    use_binary_tree();
} else if (need_fast_lookups && !need_ordering) {
    // Хеш-таблица лучше
    use_hash_table();
}
```

### 2. Обработка ключей

```c
// Хранение ключей отдельно от данных
typedef struct tree_node {
    char *key;      // Ключ хранится отдельно
    void *data;     // Пользовательские данные
} tree_node_t;

// При вставке
tree_node_t *node = malloc(sizeof(tree_node_t));
node->key = strdup(key);
node->data = user_data;

tree = dap_binary_tree_insert(tree, node->key, node);
```

### 3. Оптимизация производительности

```c
// Для часто используемых деревьев
if (tree_height(tree) > MAX_HEIGHT) {
    // Рассмотреть ребалансировку или другую структуру
    consider_rebalancing();
}

// Кэширование часто используемых элементов
static dap_binary_tree_t *cache = NULL;

// Для неизменяемых деревьев рассмотреть другие структуры
```

## Заключение

Модуль `dap_binary_tree` предоставляет эффективную реализацию бинарных деревьев поиска для DAP SDK:

- **Простота использования**: Интуитивный API с понятными функциями
- **Гибкость**: Поддержка произвольных пользовательских данных
- **Производительность**: Логарифмическая сложность для основных операций
- **Интеграция**: Совместная работа с другими структурами данных DAP SDK
- **Надежность**: Проверенные алгоритмы с защитой от ошибок

### Когда использовать:

- Нужен упорядоченный доступ к данным
- Требуется эффективный поиск с логарифмической сложностью
- Данные поступают в случайном порядке
- Важна предсказуемость операций

### Когда не использовать:

- Данные поступают в отсортированном порядке (риск вырождения)
- Требуется гарантированная производительность O(log n)
- Критична максимальная скорость вставки/поиска
- Дерево будет очень большим (риск переполнения стека)

Для получения дополнительной информации смотрите:
- `dap_binary_tree.h` - полный API бинарных деревьев
- `dap_list.h` - интеграция со связными списками
- Примеры в директории `examples/binary_tree/`
- Документацию по алгоритмам деревьев поиска
