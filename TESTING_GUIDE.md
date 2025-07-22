# 🧪 **РУКОВОДСТВО ПО ТЕСТИРОВАНИЮ PYTHON-DAP SDK**

⚠️ **КРИТИЧЕСКОЕ ПРАВИЛО:** Тесты Python-DAP можно запускать **ТОЛЬКО через CMake/CTest** ⚠️

---

## 🎯 **ПОЧЕМУ ТОЛЬКО CTEST?**

Python-DAP SDK - это **нативный C extension** модуль с встроенным статическим DAP SDK. При сборке через CMake:

✅ **Правильно настраиваются:**
- Пути к нативным библиотекам
- Линковка с DAP SDK
- Python paths и зависимости  
- Виртуальное окружение для тестов
- Покрытие кода

❌ **При прямом запуске pytest:**
- Могут отсутствовать нативные функции
- Неправильные пути к модулям
- Неполная сборка C extensions
- Проблемы с зависимостями

---

## ✅ **ПРАВИЛЬНЫЙ ПРОЦЕСС ТЕСТИРОВАНИЯ**

### **1. Сборка проекта**
```bash
cd plugins/plugin-python/python-dap
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
make -j$(nproc)
```

### **2. Запуск всех тестов**
```bash
ctest --verbose
```

### **3. Запуск специфичных тестов**
```bash
# Только unit тесты
ctest -R unit --verbose

# Только integration тесты  
ctest -R integration --verbose

# Только regression тесты
ctest -R regression --verbose

# Повторный запуск провалившихся тестов
ctest --rerun-failed --output-on-failure
```

### **4. Просмотр результатов**
```bash
# Подробные логи тестов
cat Testing/Temporary/LastTest.log

# Результаты покрытия кода (если включено)
ls htmlcov/index.html
```

---

## 🚨 **НЕ РЕКОМЕНДУЕТСЯ**

### ❌ **Прямой запуск pytest:**
```bash
python -m pytest tests/  # Может не найти все функции
```

### ❌ **Запуск через setup.py:**
```bash
python setup.py test  # Неполная сборка
```

### ❌ **Без предварительной сборки:**
```bash
pytest  # Модуль не будет найден
```

---

## 🔄 **FALLBACK МЕТОД (если CTest недоступен)**

Если по каким-то причинам CTest не работает, можно использовать:

```bash
# Собрать модуль через CMake
cd plugins/plugin-python/python-dap
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Вернуться в корень проекта
cd ..

# Установить PYTHONPATH и запустить тесты
export PYTHONPATH=$(pwd)/build/lib:$PYTHONPATH
python -m pytest tests/ --tb=long --no-cov -v
```

⚠️ **Предупреждение:** Этот метод менее надежен и некоторые функции могут отсутствовать.

---

## 📊 **ОЖИДАЕМЫЕ РЕЗУЛЬТАТЫ**

### **✅ Успешная сборка:**
- Модуль: `lib/python_dapcpython-311-x86_64-linux-gnu.so` создан
- Размер: ~2.8MB  
- Импорт: `import python_dap` работает без ошибок

### **✅ Доступные функции:**
- **Config:** `py_dap_config_init`, `py_dap_config_get_item_str`, etc.
- **Client:** `dap_client_new`, `dap_client_connect_to`, `dap_client_delete`
- **Server:** `dap_server_new`, `dap_server_start`, `dap_server_stop`
- **Core:** `dap_common_init`, `dap_common_deinit`
- **Logging:** `py_dap_log_it_debug`, `py_dap_log_it_info`, etc.
- **Memory:** `py_dap_malloc`, `py_dap_free`, `py_dap_calloc`

### **📈 Ожидаемое покрытие тестов:**
- **Config тесты:** 90%+ должны пройти ✅
- **Core тесты:** 80%+ должны пройти ✅  
- **Client/Server тесты:** 70%+ должны пройти ✅
- **Stream тесты:** Могут иметь проблемы (функции в разработке)

---

## 🛠️ **ОТЛАДКА ПРОБЛЕМ**

### **Проблема: ModuleNotFoundError: No module named 'python_dap'**
```bash
# Проверить наличие модуля
ls build/lib/python_dap*.so

# Если модуль есть, пересобрать с чистого листа
rm -rf build && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
make -j$(nproc)
```

### **Проблема: ImportError: cannot import name 'function_name'**
```bash
# Проверить доступные функции в модуле
python -c "import sys; sys.path.insert(0, 'build/lib'); import python_dap; print('\\n'.join([name for name in dir(python_dap) if not name.startswith('_')]))"
```

### **Проблема: CTest не находит тесты**
```bash
# Убедиться что BUILD_TESTS=ON
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON

# Проверить список доступных тестов
ctest --show-only
```

---

## 📋 **CHECKLIST ПЕРЕД КОММИТОМ**

- [ ] ✅ Сборка через CMake проходит без ошибок
- [ ] ✅ `import python_dap` работает
- [ ] ✅ Основные функции доступны (config, client, server)
- [ ] ✅ CTest запускается и показывает результаты
- [ ] ✅ Покрытие кода > 70% (если настроено)
- [ ] ✅ Нет критических ошибок в тестах
- [ ] ✅ Документация обновлена при изменении API

---

## 🚀 **ИНТЕГРАЦИЯ С CI/CD**

Для автоматизированного тестирования в CI/CD pipeline:

```yaml
# .github/workflows/test-python-dap.yml
name: Test Python DAP
on: [push, pull_request]

jobs:
  test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      - name: Install dependencies
        run: sudo apt-get install cmake build-essential python3-dev
      
      - name: Build Python DAP
        run: |
          cd plugins/plugin-python/python-dap
          mkdir build && cd build
          cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
          make -j$(nproc)
      
      - name: Run tests
        run: |
          cd plugins/plugin-python/python-dap/build
          ctest --verbose --output-on-failure
```

---

**Версия документа:** 1.0  
**Дата создания:** 21 января 2025  
**Статус:** Актуально для Python-DAP SDK v3.0.0+ 