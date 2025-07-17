#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dap_common.h"
#include "dap_enc_chipmunk.h"
#include "dap_enc_chipmunk_test.h"

#define LOG_TAG "chipmunk_test_main"

/**
 * @brief Тестовая функция только для проверки подписи
 * 
 * @return int Результат выполнения (0 - успех)
 */
int test_chipmunk_signature_only(void) {
    // Инициализируем модуль Chipmunk, если он еще не инициализирован
    dap_enc_chipmunk_init();
    
    log_it(L_NOTICE, "Тестирование подписи Chipmunk...");
    
    // Создаем ключ
    dap_enc_key_t *l_key = dap_enc_key_new(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK);
    if (!l_key) {
        log_it(L_ERROR, "Не удалось создать ключ");
        return -1;
    }
    
    const char *test_data = "Test message for signing";
    size_t test_data_size = strlen(test_data);
    
    // Получаем размер подписи
    size_t l_sign_size = dap_enc_chipmunk_calc_signature_size();
    uint8_t *l_sign = DAP_NEW_SIZE(uint8_t, l_sign_size);
    
    if (!l_sign) {
        log_it(L_ERROR, "Не удалось выделить память для подписи");
        dap_enc_key_delete(l_key);
        return -1;
    }
    
    // Создаем подпись
    log_it(L_NOTICE, "Создаем подпись...");
    int l_sign_res = l_key->sign_get(l_key, (uint8_t*)test_data, test_data_size, l_sign, l_sign_size);
    
    if (l_sign_res != 0) {
        log_it(L_ERROR, "Не удалось создать подпись, код ошибки: %d", l_sign_res);
        DAP_DELETE(l_sign);
        dap_enc_key_delete(l_key);
        return -1;
    }
    
    log_it(L_NOTICE, "Подпись успешно создана (код ошибки: 0)");
    
    // Проверяем подпись
    log_it(L_NOTICE, "Проверяем подпись...");
    int l_verify_res = l_key->sign_verify(l_key, (uint8_t*)test_data, test_data_size, l_sign, l_sign_size);
    
    if (l_verify_res != 0) {
        log_it(L_ERROR, "Проверка подписи не удалась, код ошибки: %d", l_verify_res);
        DAP_DELETE(l_sign);
        dap_enc_key_delete(l_key);
        return -1;
    }
    
    log_it(L_NOTICE, "Подпись успешно проверена");
    
    DAP_DELETE(l_sign);
    dap_enc_key_delete(l_key);
    
    return 0;
}

/**
 * @brief Entry point for Chipmunk unit tests
 * 
 * @return int Exit code (0 - success)
 */
int main(void) {
    // Initialize logging - check environment variable for debug level
    dap_log_level_set(L_INFO);
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
   
    // Initialize Chipmunk module
    dap_enc_chipmunk_init();
    
    log_it(L_NOTICE, "Starting Chipmunk cryptographic module tests");
    
    // Run all Chipmunk tests
    int result = dap_enc_chipmunk_tests_run();
    
    // If standard tests passed, run additional test for signature
    if (result == 0) {
        log_it(L_NOTICE, "Запуск отдельного теста для подписи Chipmunk");
        result = test_chipmunk_signature_only();
    }
    
    // Report results
    if (result == 0) {
        log_it(L_NOTICE, "All Chipmunk cryptographic tests PASSED");
    } else {
        log_it(L_ERROR, "Some Chipmunk tests FAILED! Error code: %d", result);
    }
    
    return result;
} 
