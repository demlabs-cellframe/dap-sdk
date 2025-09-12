/**
 * @file main.c
 * @brief Простейший пример использования DAP SDK
 *
 * Этот пример демонстрирует базовую инициализацию и завершение работы с DAP SDK.
 * Он может служить отправной точкой для разработки более сложных приложений.
 */

#include "dap_common.h"
#include "dap_time.h"
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
    int init_result = dap_common_init("DAP Hello World", NULL);
    if (init_result != 0) {
        fprintf(stderr, "ERROR: Failed to initialize DAP SDK (code: %d)\n", init_result);
        return EXIT_FAILURE;
    }
    printf("✓ DAP SDK initialized successfully\n");

    // Вывод информации о версии
    printf("\nDAP SDK Version Information:\n");
    printf("  Version: %s\n", DAP_SDK_VERSION);

    // Демонстрация работы с памятью
    printf("\nMemory Management Example:\n");
    void *test_memory = DAP_NEW_Z_SIZE(char, 100);
    if (test_memory) {
        // Security fix: use safe string copy
        dap_strncpy((char*)test_memory, "Hello from DAP SDK!", 99);
        printf("  Allocated memory: %s\n", (char*)test_memory);
        DAP_FREE(test_memory);
        printf("  ✓ Memory freed successfully\n");
    } else {
        printf("  ✗ Failed to allocate memory\n");
    }

    // Демонстрация работы со временем
    printf("\nTime Management Example:\n");
    dap_time_t current_time = dap_time_now();
    char time_str[64];
    size_t written = dap_time_to_str_rfc822(time_str, sizeof(time_str), current_time);
    if (written > 0) {
        printf("  Current time: %s\n", time_str);
    } else {
        printf("  Current timestamp: %" PRIu64 "\n", current_time);
    }

    // Завершение работы с DAP SDK
    printf("\nShutting down DAP SDK...\n");
    dap_common_deinit();
    printf("✓ DAP SDK shut down successfully\n");

    printf("\nExample completed successfully!\n");
    printf("You can now explore more advanced DAP SDK features.\n");

    return EXIT_SUCCESS;
}
