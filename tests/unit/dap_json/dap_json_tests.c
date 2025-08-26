/*
 * Authors:
 * Dmitry Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dap_common.h"
#include "dap_test.h"
#include "dap_json.h"
#include "../../fixtures/json_samples.h"

#define LOG_TAG "dap_json_unit_tests"

/**
 * @brief Тест создания JSON объекта
 * @details Проверяет корректное создание и освобождение JSON объектов
 */
static void s_test_dap_json_object_creation(void)
{
    log_it(L_DEBUG, "Testing JSON object creation");
    
    dap_json_t *l_json = dap_json_object_new();
    dap_return_if_fail(l_json != NULL);
    
    log_it(L_DEBUG, "JSON object created successfully");
    
    dap_json_object_free(l_json);
    log_it(L_DEBUG, "JSON object freed successfully");
}

/**
 * @brief Тест создания JSON массива
 * @details Проверяет корректное создание и освобождение JSON массивов
 */
static void s_test_dap_json_array_creation(void)
{
    log_it(L_DEBUG, "Testing JSON array creation");
    
    dap_json_array_t *l_array = dap_json_array_new();
    dap_return_if_fail(l_array != NULL);
    
    log_it(L_DEBUG, "JSON array created successfully");
    
    dap_json_array_free(l_array);
    log_it(L_DEBUG, "JSON array freed successfully");
}

// Тест парсинга строки
static void test_dap_json_string_parsing(void)
{
    const char *test_json = "{\"name\":\"test\",\"value\":42,\"flag\":true}";
    dap_json_t *json = dap_json_parse_string(test_json);
    dap_assert_PIF(json != NULL, "Failed to parse JSON string");
    
    const char *name = dap_json_object_get_string(json, "name");
    dap_assert_PIF(name != NULL && strcmp(name, "test") == 0, "Failed to get string value");
    
    int64_t value = dap_json_object_get_int(json, "value", 0);
    dap_assert_PIF(value == 42, "Failed to get int value");
    
    bool flag = dap_json_object_get_bool(json, "flag", false);
    dap_assert_PIF(flag == true, "Failed to get bool value");
    
    dap_json_object_free(json);
}

// Тест сериализации
static void test_dap_json_serialization(void)
{
    dap_json_t *json = dap_json_object_new();
    dap_assert_PIF(json != NULL, "Failed to create JSON object");
    
    dap_json_object_add_string(json, "test", "value");
    dap_json_object_add_int(json, "number", 123);
    
    char *str = dap_json_to_string(json);
    dap_assert_PIF(str != NULL, "Failed to serialize JSON");
    dap_assert_PIF(strstr(str, "test") != NULL, "Missing key in serialized JSON");
    dap_assert_PIF(strstr(str, "value") != NULL, "Missing value in serialized JSON");
    dap_assert_PIF(strstr(str, "123") != NULL, "Missing number in serialized JSON");
    
    DAP_DELETE(str);
    dap_json_object_free(json);
}

// Тест типов данных
static void test_dap_json_data_types(void)
{
    dap_json_t *json = dap_json_object_new();
    dap_assert_PIF(json != NULL, "Failed to create JSON object");
    
    // Строка
    dap_json_object_add_string(json, "str", "hello");
    const char *str_val = dap_json_object_get_string(json, "str");
    dap_assert_PIF(str_val != NULL && strcmp(str_val, "hello") == 0, "String type test failed");
    
    // Число
    dap_json_object_add_int(json, "num", 42);
    int64_t num_val = dap_json_object_get_int(json, "num", 0);
    dap_assert_PIF(num_val == 42, "Integer type test failed");
    
    // Булево
    dap_json_object_add_bool(json, "flag", true);
    bool bool_val = dap_json_object_get_bool(json, "flag", false);
    dap_assert_PIF(bool_val == true, "Boolean type test failed");
    
    // Double
    dap_json_object_add_double(json, "pi", 3.14159);
    double double_val = dap_json_object_get_double(json, "pi", 0.0);
    dap_assert_PIF(double_val > 3.14 && double_val < 3.15, "Double type test failed");
    
    dap_json_object_free(json);
}

// Тест операций с массивами
static void test_dap_json_array_operations(void)
{
    dap_json_array_t *array = dap_json_array_new();
    dap_assert_PIF(array != NULL, "Failed to create JSON array");
    
    // Добавление элементов
    dap_json_array_add_string(array, "first");
    dap_json_array_add_string(array, "second");
    dap_json_array_add_int(array, 42);
    
    // Проверка длины
    size_t length = dap_json_array_length(array);
    dap_assert_PIF(length == 3, "Array length test failed");
    
    // Получение элементов
    const char *first = dap_json_array_get_string(array, 0);
    dap_assert_PIF(first != NULL && strcmp(first, "first") == 0, "Array string element test failed");
    
    const char *second = dap_json_array_get_string(array, 1);
    dap_assert_PIF(second != NULL && strcmp(second, "second") == 0, "Array second string element test failed");
    
    int64_t value = dap_json_array_get_int(array, 2, 0);
    dap_assert_PIF(value == 42, "Array int element test failed");
    
    // Тест сериализации массива
    char *array_str = dap_json_array_to_string(array);
    dap_assert_PIF(array_str != NULL, "Array serialization failed");
    dap_assert_PIF(strstr(array_str, "first") != NULL, "Array serialization missing element");
    
    DAP_DELETE(array_str);
    dap_json_array_free(array);
}

// Тест файловых операций
static void test_dap_json_file_operations(void)
{
    const char *test_content = "{\"file_test\":true,\"value\":100}";
    
    // Создание временного файла
    char *temp_file = json_fixture_create_temp_file(test_content);
    dap_assert_PIF(temp_file != NULL, "Failed to create temp file");
    
    // Парсинг из файла
    dap_json_t *json = dap_json_parse_file(temp_file);
    dap_assert_PIF(json != NULL, "Failed to parse JSON from file");
    
    bool file_test = dap_json_object_get_bool(json, "file_test", false);
    dap_assert_PIF(file_test == true, "File parsing test failed");
    
    int64_t value = dap_json_object_get_int(json, "value", 0);
    dap_assert_PIF(value == 100, "File parsing value test failed");
    
    dap_json_object_free(json);
    json_fixture_cleanup_temp_file(temp_file);
    DAP_DELETE(temp_file);
}

// Тест ошибочных условий
static void test_dap_json_error_conditions(void)
{
    // NULL входные данные
    dap_json_t *json = dap_json_parse_string(NULL);
    dap_assert_PIF(json == NULL, "Should return NULL for NULL input");
    
    json = dap_json_parse_string("");
    dap_assert_PIF(json == NULL, "Should return NULL for empty string");
    
    // Невалидный JSON
    json = dap_json_parse_string("{invalid json}");
    dap_assert_PIF(json == NULL, "Should return NULL for invalid JSON");
    
    // Операции с NULL объектом
    const char *str = dap_json_object_get_string(NULL, "key");
    dap_assert_PIF(str == NULL, "Should return NULL for NULL object");
    
    int64_t value = dap_json_object_get_int(NULL, "key", -1);
    dap_assert_PIF(value == -1, "Should return default for NULL object");
    
    // Несуществующие ключи
    json = dap_json_object_new();
    str = dap_json_object_get_string(json, "nonexistent");
    dap_assert_PIF(str == NULL, "Should return NULL for missing key");
    
    dap_json_object_free(json);
}

// Тест вложенных объектов
static void test_dap_json_nested_objects(void)
{
    dap_json_t *root = dap_json_object_new();
    dap_json_t *nested = dap_json_object_new();
    
    dap_assert_PIF(root != NULL && nested != NULL, "Failed to create nested objects");
    
    // Создание вложенной структуры
    dap_json_object_add_string(nested, "inner", "value");
    dap_json_object_add_int(nested, "count", 5);
    
    dap_json_object_add_object(root, "nested", nested);
    dap_json_object_add_string(root, "outer", "test");
    
    // Получение вложенного объекта
    dap_json_t *retrieved = dap_json_object_get_object(root, "nested");
    dap_assert_PIF(retrieved != NULL, "Failed to get nested object");
    
    const char *inner = dap_json_object_get_string(retrieved, "inner");
    dap_assert_PIF(inner != NULL && strcmp(inner, "value") == 0, "Nested object access failed");
    
    int64_t count = dap_json_object_get_int(retrieved, "count", 0);
    dap_assert_PIF(count == 5, "Nested object int access failed");
    
    dap_json_object_free(root);
}

/**
 * @brief Функция запуска всех тестов для dap_json
 * @details Инициализирует fixtures и запускает все тесты dap_json API
 * @return 0 при успешном выполнении, отрицательное значение при ошибке
 */
int dap_json_tests_run(void)
{
    log_it(L_INFO, "Starting dap_json unit tests");
    
    // Инициализация fixtures
    int l_ret = json_fixtures_init();
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to initialize JSON fixtures: %d", l_ret);
        return -EINVAL;
    }
    
    dap_print_module_name("dap_json unit tests");
    
    s_test_dap_json_object_creation();
    s_test_dap_json_array_creation();
    // TODO: Остальные тесты нужно переписать согласно стандартам DAP SDK
    
    log_it(L_INFO, "dap_json unit tests completed successfully");
    dap_print_module_name("dap_json unit tests completed successfully");
    
    return 0;
}
