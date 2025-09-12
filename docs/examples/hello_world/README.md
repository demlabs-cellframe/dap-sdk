# DAP SDK Hello World Example

## Описание

Этот пример демонстрирует самый простой способ начать работу с DAP SDK. Он показывает базовую инициализацию SDK, работу с памятью и временем, а также правильное завершение работы.

## Что делает пример

1. **Инициализация DAP SDK** - показывает как правильно инициализировать SDK
2. **Информация о версии** - выводит информацию о сборке и версии SDK
3. **Управление памятью** - демонстрирует безопасное выделение и освобождение памяти
4. **Работа со временем** - показывает как получить текущее время
5. **Корректное завершение** - демонстрирует правильную очистку ресурсов

## Сборка и запуск

### Требования
- DAP SDK установлен и настроен
- Компилятор C (GCC или Clang)
- CMake 3.10+

### Сборка

```bash
# Из директории примера
mkdir build
cd build
cmake ..
make
```

### Запуск

```bash
./hello_world
```

### Ожидаемый вывод

```
DAP SDK Hello World Example
===========================

Initializing DAP SDK...
✓ DAP SDK initialized successfully

DAP SDK Version Information:
  Build: [build_info]
  Git commit: [commit_hash]

Memory Management Example:
  Allocated memory: Hello from DAP SDK!
  ✓ Memory freed successfully

Time Management Example:
  Current time: 2025-01-09 12:34:56 UTC

Shutting down DAP SDK...
✓ DAP SDK shut down successfully

Example completed successfully!
You can now explore more advanced DAP SDK features.
```

## Структура кода

### main.c
- `main()` - основная функция
- Инициализация и завершение SDK
- Демонстрация основных функций

### CMakeLists.txt
- Конфигурация сборки
- Подключение DAP SDK
- Настройки компилятора

## Следующие шаги

После изучения этого примера вы можете перейти к:

1. **Криптография**: [crypto_operations](../crypto_operations/)
2. **Сетевое взаимодействие**: [network_communication](../network_communication/)
3. **Управление ключами**: [key_management](../key_management/)

## Безопасность

Этот пример демонстрирует безопасные практики:
- Проверка возвращаемых значений
- Правильное управление памятью
- Корректная очистка ресурсов

## Устранение неполадок

### Ошибка инициализации
```
ERROR: Failed to initialize DAP SDK
```
**Решение**: Убедитесь, что DAP SDK правильно установлен и настроен.

### Ошибка компиляции
```
fatal error: dap_common.h: No such file or directory
```
**Решение**: Проверьте, что пути к заголовочным файлам DAP SDK указаны правильно в CMakeLists.txt.

## Дополнительная информация

- **Документация**: [../../README.md](../../README.md)
- **API Reference**: [../../modules/](../../modules/)
- **Архитектура**: [../../architecture.md](../../architecture.md)
