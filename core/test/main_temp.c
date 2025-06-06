#include "../include/dap_common.h"
#include "dap_log_test.h"

int main(void) {
    // Инициализация системы
    dap_common_init("core_test", NULL);
    dap_log_level_set(L_DEBUG);
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    
    printf("=== Запуск ТОЛЬКО тестов системы логирования ===\n\n");
    
    // Запускаем только тесты логирования
    dap_log_test_run();
    
    printf("\n=== Тесты логирования завершены ===\n");
    
    dap_common_deinit();
    return 0;
} 