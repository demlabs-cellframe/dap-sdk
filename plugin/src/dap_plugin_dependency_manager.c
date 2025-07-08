/**
 * @file dap_plugin_dependency_manager.c
 * @brief Implementation of Plugin Dependency Management System
 * @details Автоматическая загрузка плагинов с управлением зависимостями
 * 
 * @author Dmitriy A. Gerasimov <gerasimov.dmitriy.a@gmail.com>
 * @date 2025-01-08
 */

#include "dap_plugin_dependency_manager.h"
#include "dap_plugin_manifest.h"
#include "dap_plugin.h"
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_list.h"
#include "uthash.h"

#define LOG_TAG "dap_plugin_dependency_manager"

// Структура для хранения обработчиков типов файлов
typedef struct dap_plugin_type_handler_internal {
    char *extension;                           // Расширение файла (например, ".py")
    char *handler_name;                        // Имя плагина-обработчика
    dap_plugin_type_callbacks_t callbacks;     // Callback'и для обработки
    void *user_data;                          // Пользовательские данные
    UT_hash_handle hh;                        // Хеш таблица для быстрого поиска
} dap_plugin_type_handler_internal_t;

// Структура для статистики загрузки плагинов
typedef struct dap_plugin_load_stats {
    size_t total_loaded;                      // Всего загружено плагинов
    size_t dependencies_resolved;             // Зависимостей разрешено
    size_t circular_dependencies_found;       // Найдено циркулярных зависимостей
    size_t auto_loads_triggered;              // Автозагрузок запущено
    size_t handlers_registered;               // Зарегистрировано обработчиков
} dap_plugin_load_stats_t;

// Внутренние переменные модуля
static dap_plugin_type_handler_internal_t *s_type_handlers = NULL;
static dap_plugin_load_stats_t s_stats = {0};
static bool s_initialized = false;

/**
 * @brief Инициализация менеджера зависимостей плагинов
 * @return 0 при успехе, отрицательное значение при ошибке
 */
int dap_plugin_dependency_manager_init(void)
{
    if (s_initialized) {
        log_it(L_WARNING, "Plugin dependency manager already initialized");
        return 0;
    }

    // Инициализация хеш таблицы
    s_type_handlers = NULL;
    
    // Сброс статистики
    memset(&s_stats, 0, sizeof(s_stats));
    
    s_initialized = true;
    log_it(L_INFO, "Plugin dependency manager initialized");
    return 0;
}

/**
 * @brief Деинициализация менеджера зависимостей плагинов
 */
void dap_plugin_dependency_manager_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    // Очистка хеш таблицы обработчиков
    dap_plugin_type_handler_internal_t *current_handler, *tmp;
    HASH_ITER(hh, s_type_handlers, current_handler, tmp) {
        HASH_DEL(s_type_handlers, current_handler);
        DAP_DELETE(current_handler->extension);
        DAP_DELETE(current_handler->handler_name);
        DAP_DELETE(current_handler);
    }
    
    s_initialized = false;
    log_it(L_INFO, "Plugin dependency manager deinitialized");
}

/**
 * @brief Регистрация обработчика типа файлов
 * @param a_extension Расширение файла (например, ".py")
 * @param a_handler_name Имя плагина-обработчика
 * @param a_callbacks Callback'и для обработки файлов
 * @param a_user_data Пользовательские данные
 * @return 0 при успехе, отрицательное значение при ошибке
 */
int dap_plugin_register_type_handler(const char *a_extension,
                                   const char *a_handler_name,
                                   dap_plugin_type_callbacks_t *a_callbacks,
                                   void *a_user_data)
{
    // Валидация параметров
    if (!a_extension || !a_handler_name || !a_callbacks) {
        log_it(L_ERROR, "Invalid parameters for type handler registration");
        return -1;
    }

    if (!s_initialized) {
        log_it(L_ERROR, "Plugin dependency manager not initialized");
        return -2;
    }

    // Проверка на дубликаты
    dap_plugin_type_handler_internal_t *existing_handler;
    HASH_FIND_STR(s_type_handlers, a_extension, existing_handler);
    if (existing_handler) {
        log_it(L_WARNING, "Type handler for extension '%s' already exists, replacing", a_extension);
        HASH_DEL(s_type_handlers, existing_handler);
        DAP_DELETE(existing_handler->extension);
        DAP_DELETE(existing_handler->handler_name);
        DAP_DELETE(existing_handler);
    }

    // Создание нового обработчика
    dap_plugin_type_handler_internal_t *handler = DAP_NEW_Z(dap_plugin_type_handler_internal_t);
    if (!handler) {
        log_it(L_ERROR, "Memory allocation failed for type handler");
        return -3;
    }

    handler->extension = dap_strdup(a_extension);
    handler->handler_name = dap_strdup(a_handler_name);
    if (a_callbacks) {
        memcpy(&handler->callbacks, a_callbacks, sizeof(dap_plugin_type_callbacks_t));
    }
    handler->user_data = a_user_data;

    // Добавление в хеш таблицу
    HASH_ADD_STR(s_type_handlers, extension, handler);
    s_stats.handlers_registered++;

    log_it(L_INFO, "Registered type handler: '%s' -> '%s'", a_extension, a_handler_name);
    return 0;
}

/**
 * @brief Получение обработчика для типа файла
 * @param a_extension Расширение файла
 * @return Имя обработчика или NULL если не найден
 */
const char *dap_plugin_find_type_handler(const char *a_extension)
{
    if (!a_extension || !s_initialized) {
        return NULL;
    }

    dap_plugin_type_handler_internal_t *handler;
    HASH_FIND_STR(s_type_handlers, a_extension, handler);
    return handler ? handler->handler_name : NULL;
}

/**
 * @brief Обнаружение файлов плагинов в директории
 * @param a_directory Путь к директории
 * @param a_recursive Рекурсивный поиск
 * @param a_results Массив результатов (выделяется внутри функции)
 * @param a_results_count Количество найденных файлов
 * @return 0 при успехе, отрицательное значение при ошибке
 */
int dap_plugin_scan_directory(const char *a_directory,
                            bool a_recursive,
                            dap_plugin_detection_result_t **a_results,
                            size_t *a_results_count)
{
    if (!a_directory || !a_results || !a_results_count) {
        log_it(L_ERROR, "Invalid parameters for directory scanning");
        return -1;
    }

    if (!s_initialized) {
        log_it(L_ERROR, "Plugin dependency manager not initialized");
        return -2;
    }

    *a_results = NULL;
    *a_results_count = 0;

    // Получение списка файлов в директории
    dap_list_t *files = dap_get_files_list(a_directory, a_recursive);
    if (!files) {
        log_it(L_DEBUG, "No files found in directory: %s", a_directory);
        return 0;
    }

    // Подсчет релевантных файлов
    size_t relevant_count = 0;
    for (dap_list_t *it = files; it; it = it->next) {
        const char *file_path = (const char*)it->data;
        const char *ext = strrchr(file_path, '.');
        if (ext && dap_plugin_find_type_handler(ext)) {
            relevant_count++;
        }
    }

    if (relevant_count == 0) {
        dap_list_free(files);
        return 0;
    }

    // Выделение памяти для результатов
    dap_plugin_detection_result_t *results = DAP_NEW_Z_SIZE(dap_plugin_detection_result_t, 
                                                           relevant_count * sizeof(dap_plugin_detection_result_t));
    if (!results) {
        log_it(L_ERROR, "Memory allocation failed for detection results");
        dap_list_free(files);
        return -3;
    }

    // Заполнение результатов
    size_t result_index = 0;
    for (dap_list_t *it = files; it; it = it->next) {
        const char *file_path = (const char*)it->data;
        const char *ext = strrchr(file_path, '.');
        
        if (ext) {
            const char *handler_name = dap_plugin_find_type_handler(ext);
            if (handler_name) {
                dap_plugin_detection_result_t *result = &results[result_index];
                result->file_path = dap_strdup(file_path);
                result->extension = dap_strdup(ext);
                result->handler_name = dap_strdup(handler_name);
                result->confidence = 1.0f; // 100% уверенность для точных совпадений
                result_index++;
            }
        }
    }

    dap_list_free(files);
    *a_results = results;
    *a_results_count = relevant_count;

    log_it(L_INFO, "Scanned directory '%s': found %zu plugin files", a_directory, relevant_count);
    return 0;
}

/**
 * @brief Освобождение результатов обнаружения
 * @param a_results Массив результатов
 * @param a_results_count Количество результатов
 */
void dap_plugin_detection_results_free(dap_plugin_detection_result_t *a_results, size_t a_results_count)
{
    if (!a_results || a_results_count == 0) {
        return;
    }

    for (size_t i = 0; i < a_results_count; i++) {
        DAP_DELETE(a_results[i].file_path);
        DAP_DELETE(a_results[i].extension);
        DAP_DELETE(a_results[i].handler_name);
    }

    DAP_DELETE(a_results);
}

/**
 * @brief Внутренняя функция для DFS обхода зависимостей
 * @param a_plugin_name Имя плагина
 * @param a_manifests Массив манифестов
 * @param a_manifests_count Количество манифестов
 * @param a_visited Массив посещенных плагинов
 * @param a_in_stack Массив плагинов в стеке (для поиска циклов)
 * @param a_sorted_names Отсортированный список имен
 * @param a_sorted_count Текущее количество в отсортированном списке
 * @return 0 при успехе, -1 при циркулярной зависимости
 */
static int dap_plugin_dependency_dfs(const char *a_plugin_name,
                                    dap_plugin_manifest_t *a_manifests,
                                    size_t a_manifests_count,
                                    bool *a_visited,
                                    bool *a_in_stack,
                                    char **a_sorted_names,
                                    size_t *a_sorted_count)
{
    // Поиск индекса плагина в массиве манифестов
    size_t plugin_index = SIZE_MAX;
    for (size_t i = 0; i < a_manifests_count; i++) {
        if (strcmp(a_manifests[i].name, a_plugin_name) == 0) {
            plugin_index = i;
            break;
        }
    }

    if (plugin_index == SIZE_MAX) {
        log_it(L_ERROR, "Plugin '%s' not found in manifests", a_plugin_name);
        return -1;
    }

    // Проверка циркулярной зависимости
    if (a_in_stack[plugin_index]) {
        log_it(L_ERROR, "Circular dependency detected for plugin '%s'", a_plugin_name);
        s_stats.circular_dependencies_found++;
        return -1;
    }

    if (a_visited[plugin_index]) {
        return 0; // Уже обработано
    }

    // Отметка как посещенный и добавление в стек
    a_visited[plugin_index] = true;
    a_in_stack[plugin_index] = true;

    // Рекурсивная обработка зависимостей
    dap_plugin_manifest_t *manifest = &a_manifests[plugin_index];
    for (size_t i = 0; i < manifest->dependencies_count; i++) {
        const char *dep_name = manifest->dependencies_names[i];
        if (dap_plugin_dependency_dfs(dep_name, a_manifests, a_manifests_count,
                                    a_visited, a_in_stack, a_sorted_names, a_sorted_count) != 0) {
            return -1;
        }
    }

    // Удаление из стека и добавление в отсортированный список
    a_in_stack[plugin_index] = false;
    a_sorted_names[*a_sorted_count] = dap_strdup(a_plugin_name);
    (*a_sorted_count)++;

    return 0;
}

/**
 * @brief Создание отсортированного списка плагинов с учетом зависимостей
 * @param a_manifests Массив манифестов плагинов
 * @param a_manifests_count Количество манифестов
 * @param a_sorted_names Отсортированный список имен (выделяется внутри функции)
 * @param a_sorted_count Количество плагинов в отсортированном списке
 * @return 0 при успехе, отрицательное значение при ошибке
 */
int dap_plugin_create_dependency_order(dap_plugin_manifest_t *a_manifests,
                                     size_t a_manifests_count,
                                     char ***a_sorted_names,
                                     size_t *a_sorted_count)
{
    if (!a_manifests || !a_sorted_names || !a_sorted_count) {
        log_it(L_ERROR, "Invalid parameters for dependency ordering");
        return -1;
    }

    *a_sorted_names = NULL;
    *a_sorted_count = 0;

    if (a_manifests_count == 0) {
        return 0;
    }

    // Выделение памяти для рабочих массивов
    bool *visited = DAP_NEW_Z_SIZE(bool, a_manifests_count * sizeof(bool));
    bool *in_stack = DAP_NEW_Z_SIZE(bool, a_manifests_count * sizeof(bool));
    char **sorted_names = DAP_NEW_Z_SIZE(char*, a_manifests_count * sizeof(char*));

    if (!visited || !in_stack || !sorted_names) {
        log_it(L_ERROR, "Memory allocation failed for dependency sorting");
        DAP_DELETE(visited);
        DAP_DELETE(in_stack);
        DAP_DELETE(sorted_names);
        return -2;
    }

    size_t sorted_count = 0;

    // DFS для каждого непосещенного плагина
    for (size_t i = 0; i < a_manifests_count; i++) {
        if (!visited[i]) {
            if (dap_plugin_dependency_dfs(a_manifests[i].name, a_manifests, a_manifests_count,
                                        visited, in_stack, sorted_names, &sorted_count) != 0) {
                // Очистка при ошибке
                for (size_t j = 0; j < sorted_count; j++) {
                    DAP_DELETE(sorted_names[j]);
                }
                DAP_DELETE(visited);
                DAP_DELETE(in_stack);
                DAP_DELETE(sorted_names);
                return -3;
            }
        }
    }

    DAP_DELETE(visited);
    DAP_DELETE(in_stack);

    *a_sorted_names = sorted_names;
    *a_sorted_count = sorted_count;

    s_stats.dependencies_resolved += sorted_count;
    log_it(L_INFO, "Created dependency order for %zu plugins", sorted_count);
    return 0;
}

/**
 * @brief Автоматическая загрузка необходимых обработчиков
 * @param a_results Результаты обнаружения файлов
 * @param a_results_count Количество результатов
 * @return 0 при успехе, отрицательное значение при ошибке
 */
int dap_plugin_auto_load_handlers(dap_plugin_detection_result_t *a_results,
                                size_t a_results_count)
{
    if (!a_results || a_results_count == 0) {
        log_it(L_DEBUG, "No detection results for auto-loading");
        return 0;
    }

    if (!s_initialized) {
        log_it(L_ERROR, "Plugin dependency manager not initialized");
        return -1;
    }

    // Создание множества уникальных обработчиков
    char **unique_handlers = DAP_NEW_Z_SIZE(char*, a_results_count * sizeof(char*));
    size_t unique_count = 0;

    for (size_t i = 0; i < a_results_count; i++) {
        const char *handler_name = a_results[i].handler_name;
        
        // Проверка на дубликаты
        bool found = false;
        for (size_t j = 0; j < unique_count; j++) {
            if (strcmp(unique_handlers[j], handler_name) == 0) {
                found = true;
                break;
            }
        }
        
        if (!found) {
            unique_handlers[unique_count] = dap_strdup(handler_name);
            unique_count++;
        }
    }

    log_it(L_INFO, "Auto-loading %zu unique handlers", unique_count);

    // Загрузка каждого уникального обработчика
    for (size_t i = 0; i < unique_count; i++) {
        const char *handler_name = unique_handlers[i];
        
        // Проверка, что плагин еще не загружен
        dap_plugin_manifest_t *manifest = dap_plugin_manifest_find(handler_name);
        if (manifest) {
            log_it(L_INFO, "Auto-loading handler plugin: %s", handler_name);
            
            // Попытка загрузки плагина
            if (dap_plugin_start(handler_name) == 0) {
                s_stats.auto_loads_triggered++;
                log_it(L_INFO, "Successfully auto-loaded handler: %s", handler_name);
            } else {
                log_it(L_ERROR, "Failed to auto-load handler: %s", handler_name);
            }
        } else {
            log_it(L_WARNING, "Handler plugin '%s' not found in manifests", handler_name);
        }
    }

    // Очистка памяти
    for (size_t i = 0; i < unique_count; i++) {
        DAP_DELETE(unique_handlers[i]);
    }
    DAP_DELETE(unique_handlers);

    return 0;
}

/**
 * @brief Загрузка плагина с учетом зависимостей
 * @param a_plugin_name Имя плагина
 * @param a_plugin_path Путь к плагину (может быть NULL для поиска)
 * @return 0 при успехе, отрицательное значение при ошибке
 */
int dap_plugin_load_with_dependencies(const char *a_plugin_name,
                                    const char *a_plugin_path)
{
    if (!a_plugin_name) {
        log_it(L_ERROR, "Invalid plugin name for dependency loading");
        return -1;
    }

    if (!s_initialized) {
        log_it(L_ERROR, "Plugin dependency manager not initialized");
        return -2;
    }

    // Получение манифеста плагина
    dap_plugin_manifest_t *manifest = dap_plugin_manifest_find(a_plugin_name);
    if (!manifest) {
        log_it(L_ERROR, "Unable to get manifest for plugin: %s", a_plugin_name);
        return -3;
    }

    // Загрузка всех зависимостей
    for (size_t i = 0; i < manifest->dependencies_count; i++) {
        const char *dep_name = manifest->dependencies_names[i];
        
        // Проверка статуса зависимости
        dap_plugin_status_t status = dap_plugin_status(dep_name);
        if (status != STATUS_RUNNING) {
            log_it(L_INFO, "Loading dependency: %s for plugin: %s", dep_name, a_plugin_name);
            
            // Рекурсивная загрузка зависимости
            if (dap_plugin_load_with_dependencies(dep_name, NULL) != 0) {
                log_it(L_ERROR, "Failed to load dependency: %s", dep_name);
                return -4;
            }
        }
    }

    // Загрузка самого плагина
    dap_plugin_status_t status = dap_plugin_status(a_plugin_name);
    if (status != STATUS_RUNNING) {
        log_it(L_INFO, "Loading plugin: %s", a_plugin_name);
        
        if (dap_plugin_start(a_plugin_name) != 0) {
            log_it(L_ERROR, "Failed to load plugin: %s", a_plugin_name);
            return -5;
        }
        
        s_stats.total_loaded++;
        log_it(L_INFO, "Successfully loaded plugin with dependencies: %s", a_plugin_name);
    }

    return 0;
}

/**
 * @brief Получение статистики загрузки плагинов
 * @return Структура со статистикой
 */
dap_plugin_load_stats_t dap_plugin_get_load_stats(void)
{
    return s_stats;
}

/**
 * @brief Вывод отладочной информации о менеджере зависимостей
 */
void dap_plugin_dependency_manager_debug_print(void)
{
    if (!s_initialized) {
        log_it(L_INFO, "Plugin dependency manager not initialized");
        return;
    }

    log_it(L_INFO, "=== Plugin Dependency Manager Debug Info ===");
    log_it(L_INFO, "Initialized: %s", s_initialized ? "YES" : "NO");
    log_it(L_INFO, "Statistics:");
    log_it(L_INFO, "  Total loaded: %zu", s_stats.total_loaded);
    log_it(L_INFO, "  Dependencies resolved: %zu", s_stats.dependencies_resolved);
    log_it(L_INFO, "  Circular dependencies found: %zu", s_stats.circular_dependencies_found);
    log_it(L_INFO, "  Auto-loads triggered: %zu", s_stats.auto_loads_triggered);
    log_it(L_INFO, "  Handlers registered: %zu", s_stats.handlers_registered);

    log_it(L_INFO, "Registered type handlers:");
    dap_plugin_type_handler_internal_t *handler, *tmp;
    HASH_ITER(hh, s_type_handlers, handler, tmp) {
        log_it(L_INFO, "  %s -> %s", handler->extension, handler->handler_name);
    }
    log_it(L_INFO, "=== End Debug Info ===");
} 