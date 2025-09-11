/**
 * @file main.c
 * @brief Простейший пример использования DAP SDK
 *
 * Этот пример демонстрирует базовую инициализацию и завершение работы с DAP SDK.
 * Он может служить отправной точкой для разработки более сложных приложений.
 */

#include "dap_common.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Точка входа в приложение
 *
 * @return 0 при успешном выполнении, -1 при ошибке
 */
int main(int argc, char *argv[]) {
    (void)argc;  // Подавление предупреждения о неиспользуемом параметре
    (void)argv;  // Подавление предупреждения о неиспользуемом параметре

    printf("DAP SDK Hello World Example\n");
    printf("===========================\n\n");

    // Инициализация DAP SDK
    printf("Initializing DAP SDK...\n");
    int init_result = dap_init();
    if (init_result != 0) {
        fprintf(stderr, "ERROR: Failed to initialize DAP SDK (code: %d)\n", init_result);
        return EXIT_FAILURE;
    }
    printf("✓ DAP SDK initialized successfully\n");

    // Вывод информации о версии
    printf("\nDAP SDK Version Information:\n");
    printf("  Build: %s\n", DAP_BUILD_INFO);
    printf("  Git commit: %s\n", DAP_GIT_COMMIT_HASH);

    // Демонстрация работы с памятью
    printf("\nMemory Management Example:\n");
    void *test_memory = DAP_NEW(char, 100);
    if (test_memory) {
        strcpy((char*)test_memory, "Hello from DAP SDK!");
        printf("  Allocated memory: %s\n", (char*)test_memory);
        DAP_DELETE(test_memory);
        printf("  ✓ Memory freed successfully\n");
    } else {
        printf("  ✗ Failed to allocate memory\n");
    }

    // Демонстрация работы со временем
    printf("\nTime Management Example:\n");
    dap_time_t current_time = dap_time_now();
    char time_str[64];
    dap_time_to_string(current_time, time_str, sizeof(time_str));
    printf("  Current time: %s\n", time_str);

    // Завершение работы с DAP SDK
    printf("\nShutting down DAP SDK...\n");
    dap_deinit();
    printf("✓ DAP SDK shut down successfully\n");

    printf("\nExample completed successfully!\n");
    printf("You can now explore more advanced DAP SDK features.\n");

    return EXIT_SUCCESS;
}
