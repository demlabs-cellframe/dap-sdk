#include "dap_log_test.h"
#include <math.h>

#define LOG_TAG "dap_log_test"

static void s_test_log_format_default() {
    dap_print_module_name("dap_log_format");
    
    // Тестируем DEFAULT формат
    dap_log_set_format(DAP_LOG_FORMAT_DEFAULT);
    dap_assert(dap_log_get_format() == DAP_LOG_FORMAT_DEFAULT, "Check DEFAULT format setting");
    
    printf("      Testing DEFAULT format (with timestamp):\n");
    log_it(L_INFO, "Тестовое сообщение в DEFAULT формате");
    log_it(L_WARNING, "Предупреждение с полными метками");
}

static void s_test_log_format_simple() {
    printf("\n      Testing SIMPLE format (for unit tests):\n");
    
    // Тестируем SIMPLE формат
    dap_log_set_format(DAP_LOG_FORMAT_SIMPLE);
    dap_assert(dap_log_get_format() == DAP_LOG_FORMAT_SIMPLE, "Check SIMPLE format setting");
    
    log_it(L_INFO, "Сообщение в SIMPLE формате");
    log_it(L_ERROR, "Ошибка в простом формате");
}

static void s_test_log_format_no_time() {
    printf("\n      Testing NO_TIME format:\n");
    
    // Тестируем NO_TIME формат
    dap_log_set_format(DAP_LOG_FORMAT_NO_TIME);
    dap_assert(dap_log_get_format() == DAP_LOG_FORMAT_NO_TIME, "Check NO_TIME format setting");
    
    log_it(L_INFO, "Сообщение без времени");
    log_it(L_WARNING, "Предупреждение без времени");
}

static void s_test_log_format_clean() {
    printf("\n      Testing NO_PREFIX format (clean):\n");
    
    // Тестируем NO_PREFIX формат
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
    dap_assert(dap_log_get_format() == DAP_LOG_FORMAT_NO_PREFIX, "Check NO_PREFIX format setting");
    
    log_it(L_INFO, "Чистое сообщение без префиксов");
    log_it(L_ERROR, "Чистая ошибка");
}

static void s_test_log_simple_for_tests() {
    printf("\n      Testing convenience function for tests:\n");
    
    // Тестируем удобную функцию для unit тестов
    dap_log_set_simple_for_tests(true);
    dap_assert(dap_log_get_format() == DAP_LOG_FORMAT_SIMPLE, "Check simple_for_tests() sets SIMPLE format");
    
    log_it(L_INFO, "Сообщение через simple_for_tests(true)");
    
    // Возвращаем DEFAULT
    dap_log_set_simple_for_tests(false);
    dap_assert(dap_log_get_format() == DAP_LOG_FORMAT_DEFAULT, "Check simple_for_tests(false) restores DEFAULT");
    
    log_it(L_INFO, "Сообщение после simple_for_tests(false)");
}

static void s_test_log_performance() {
    printf("\n      Testing performance with different formats:\n");
    
    const int iterations = 1000;
    clock_t start, end;
    
    // Тест производительности DEFAULT формата
    dap_log_set_format(DAP_LOG_FORMAT_DEFAULT);
    start = clock();
    for (int i = 0; i < iterations; i++) {
        log_it(L_DEBUG, "Тест производительности %d", i);
    }
    end = clock();
    double default_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    // Тест производительности SIMPLE формата
    dap_log_set_format(DAP_LOG_FORMAT_SIMPLE);
    start = clock();
    for (int i = 0; i < iterations; i++) {
        log_it(L_DEBUG, "Тест производительности %d", i);
    }
    end = clock();
    double simple_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    // Проверяем, что SIMPLE формат быстрее или сопоставим
    printf("        DEFAULT format: %.4f sec for %d logs\n", default_time, iterations);
    printf("        SIMPLE format: %.4f sec for %d logs\n", simple_time, iterations);
    
    dap_assert(simple_time <= default_time * 2.0, "SIMPLE format should not be much slower than DEFAULT");
}

static void s_test_log_integration_with_test_framework() {
    printf("\n      Testing integration with test framework:\n");
    
    // Сохраняем текущий формат
    dap_log_format_t original_format = dap_log_get_format();
    
    // Тестируем интеграцию с test framework
    dap_log_set_simple_for_tests(true);
    
    // Используем макросы test framework совместно с новой системой логирования
    log_it(L_INFO, "Интеграция с test framework работает");
    dap_assert_PIF(dap_log_get_format() == DAP_LOG_FORMAT_SIMPLE, "Integration test passed");
    
    // Восстанавливаем формат
    dap_log_set_format(original_format);
}

void dap_log_test_run() {
    dap_print_module_name("dap_log_system");
    
    printf("=== Тестирование новой системы управления форматами логирования ===\n\n");
    
    // Сохраняем исходные настройки
    dap_log_format_t original_format = dap_log_get_format();
    enum dap_log_level original_level = dap_log_level_get();
    
    // Устанавливаем уровень логирования для демонстрации
    dap_log_level_set(L_DEBUG);
    
    // Запускаем все тесты
    s_test_log_format_default();
    s_test_log_format_simple();
    s_test_log_format_no_time();
    s_test_log_format_clean();
    s_test_log_simple_for_tests();
    s_test_log_performance();
    s_test_log_integration_with_test_framework();
    
    // Восстанавливаем исходные настройки
    dap_log_set_format(original_format);
    dap_log_level_set(original_level);
    
    printf("\n=== Все тесты системы логирования завершены успешно! ===\n\n");
} 