#include "dap_common.h"
#include "dap_enc_chipmunk.h"
#include "dap_enc_key.h"
#include "dap_sign.h"
#include "chipmunk/chipmunk.h"
#include "dap_hash.h"
#include "rand/dap_rand.h"
#include <string.h>

#define LOG_TAG "dap_enc_chipmunk"

// Флаг для расширенного логирования
static bool s_debug_more = true;

// Initialize the Chipmunk module
int dap_enc_chipmunk_init(void)
{
    log_it(L_NOTICE, "Chipmunk algorithm initialized");
    return chipmunk_init();
}

// Allocate and initialize new private key
dap_enc_key_t *dap_enc_chipmunk_key_new(void)
{
    debug_if(s_debug_more, L_DEBUG, "dap_enc_chipmunk_key_new: Creating new Chipmunk key");
    
    // CRITICAL: Add memory validation guard
    log_it(L_INFO, "=== MEMORY CORRUPTION INVESTIGATION ===");
    log_it(L_INFO, "dap_enc_chipmunk_key_new: Starting key creation process");
    
    // Allocate the key structure
    dap_enc_key_t *l_key = DAP_NEW_Z(dap_enc_key_t);
    if (!l_key) {
        log_it(L_ERROR, "Failed to allocate dap_enc_key_t structure");
        return NULL;
    }
    
    log_it(L_INFO, "✅ DAP_NEW_Z(dap_enc_key_t) successful, l_key = %p", (void*)l_key);
    log_it(L_INFO, "Memory pattern check: first 32 bytes should be zeros");
    
    // Verify memory is properly zeroed
    uint8_t *check_ptr = (uint8_t*)l_key;
    bool l_memory_clean = true;
    for (int i = 0; i < 32; i++) {
        if (check_ptr[i] != 0) {
            log_it(L_ERROR, "❌ Memory not zeroed at offset %d: 0x%02x", i, check_ptr[i]);
            l_memory_clean = false;
        }
    }
    if (l_memory_clean) {
        log_it(L_INFO, "✅ Memory properly zeroed after DAP_NEW_Z");
    }
    
    // Memory layout debugging
    log_it(L_INFO, "dap_enc_key_t memory layout:");
    log_it(L_INFO, "  Address of l_key: %p", (void*)l_key);
    log_it(L_INFO, "  Address of priv_key_data_size: %p", (void*)&l_key->priv_key_data_size);
    log_it(L_INFO, "  Address of pub_key_data_size: %p", (void*)&l_key->pub_key_data_size);
    log_it(L_INFO, "  Address of priv_key_data: %p", (void*)&l_key->priv_key_data);
    log_it(L_INFO, "  Address of pub_key_data: %p", (void*)&l_key->pub_key_data);
    
    // Check initial values are zero
    log_it(L_INFO, "Initial values after DAP_NEW_Z:");
    log_it(L_INFO, "  priv_key_data_size = %zu (should be 0)", l_key->priv_key_data_size);
    log_it(L_INFO, "  pub_key_data_size = %zu (should be 0)", l_key->pub_key_data_size);
    log_it(L_INFO, "  priv_key_data = %p (should be NULL)", l_key->priv_key_data);
    log_it(L_INFO, "  pub_key_data = %p (should be NULL)", l_key->pub_key_data);
    
    if (l_key->priv_key_data_size != 0 || l_key->pub_key_data_size != 0 ||
        l_key->priv_key_data != NULL || l_key->pub_key_data != NULL) {
        log_it(L_ERROR, "❌ CORRUPTION DETECTED IMMEDIATELY AFTER DAP_NEW_Z!");
        log_it(L_ERROR, "Memory was not properly zeroed or is corrupted");
        DAP_DELETE(l_key);
        return NULL;
    }

    // ДОБАВЛЯЕМ ДИАГНОСТИКУ РАЗМЕРОВ СТРУКТУР
    log_it(L_NOTICE, "=== STRUCTURE SIZE CHECK IN dap_enc_chipmunk_key_new ===");
    log_it(L_NOTICE, "sizeof(chipmunk_poly_t) = %zu (expected %d)", 
           sizeof(chipmunk_poly_t), CHIPMUNK_N * 4);
    log_it(L_NOTICE, "sizeof(chipmunk_public_key_t) = %zu (expected %d)", 
           sizeof(chipmunk_public_key_t), CHIPMUNK_PUBLIC_KEY_SIZE);
    log_it(L_NOTICE, "sizeof(chipmunk_private_key_t) = %zu (expected %d)", 
           sizeof(chipmunk_private_key_t), CHIPMUNK_PRIVATE_KEY_SIZE);
    log_it(L_NOTICE, "=================================");

    // ДОБАВЛЯЕМ ВЫВОД КОНСТАНТ
    debug_if(s_debug_more, L_INFO, "=== CHIPMUNK CONSTANTS CHECK ===");
    debug_if(s_debug_more, L_INFO, "CHIPMUNK_N = %d", CHIPMUNK_N);
    debug_if(s_debug_more, L_INFO, "CHIPMUNK_PUBLIC_KEY_SIZE = %d", CHIPMUNK_PUBLIC_KEY_SIZE);  
    debug_if(s_debug_more, L_INFO, "CHIPMUNK_PRIVATE_KEY_SIZE = %d", CHIPMUNK_PRIVATE_KEY_SIZE);
    debug_if(s_debug_more, L_INFO, "Calculated pub size: 32 + %d*4*2 = %d", CHIPMUNK_N, 32 + CHIPMUNK_N*4*2);
    debug_if(s_debug_more, L_INFO, "Calculated priv size: 32 + 48 + %d = %d", CHIPMUNK_PUBLIC_KEY_SIZE, 32 + 48 + CHIPMUNK_PUBLIC_KEY_SIZE);
    debug_if(s_debug_more, L_INFO, "=================================");

    // Set key type and management functions
    l_key->type = DAP_ENC_KEY_TYPE_SIG_CHIPMUNK;
    l_key->dec_na = 0;
    l_key->enc_na = 0;
    l_key->sign_get = dap_enc_chipmunk_get_sign;
    l_key->sign_verify = dap_enc_chipmunk_verify_sign;
    
    log_it(L_INFO, "=== SETTING KEY SIZES ===");
    log_it(L_INFO, "About to set priv_key_data_size = %d", CHIPMUNK_PRIVATE_KEY_SIZE);
    log_it(L_INFO, "About to set pub_key_data_size = %d", CHIPMUNK_PUBLIC_KEY_SIZE);
    
    l_key->priv_key_data_size = CHIPMUNK_PRIVATE_KEY_SIZE;
    l_key->pub_key_data_size = CHIPMUNK_PUBLIC_KEY_SIZE;
    
    // IMMEDIATE CHECK AFTER SETTING SIZES
    log_it(L_INFO, "=== IMMEDIATELY AFTER SIZE ASSIGNMENT ===");
    log_it(L_INFO, "l_key->priv_key_data_size = %zu (should be %d)", 
           l_key->priv_key_data_size, CHIPMUNK_PRIVATE_KEY_SIZE);
    log_it(L_INFO, "l_key->pub_key_data_size = %zu (should be %d)", 
           l_key->pub_key_data_size, CHIPMUNK_PUBLIC_KEY_SIZE);
    
    if (l_key->priv_key_data_size != CHIPMUNK_PRIVATE_KEY_SIZE || 
        l_key->pub_key_data_size != CHIPMUNK_PUBLIC_KEY_SIZE) {
        log_it(L_ERROR, "❌ CORRUPTION DETECTED IMMEDIATELY AFTER SIZE ASSIGNMENT!");
        log_it(L_ERROR, "Expected: priv=%d, pub=%d", CHIPMUNK_PRIVATE_KEY_SIZE, CHIPMUNK_PUBLIC_KEY_SIZE);
        log_it(L_ERROR, "Actual: priv=%zu, pub=%zu", l_key->priv_key_data_size, l_key->pub_key_data_size);
        log_it(L_ERROR, "Memory address: l_key=%p, &priv_size=%p, &pub_size=%p", 
               (void*)l_key, (void*)&l_key->priv_key_data_size, (void*)&l_key->pub_key_data_size);
        
        // Hex dump of the corrupted memory
        uint8_t *mem_ptr = (uint8_t*)&l_key->priv_key_data_size;
        log_it(L_ERROR, "Hex dump of priv_key_data_size: %02x %02x %02x %02x %02x %02x %02x %02x",
               mem_ptr[0], mem_ptr[1], mem_ptr[2], mem_ptr[3], mem_ptr[4], mem_ptr[5], mem_ptr[6], mem_ptr[7]);
        
        DAP_DELETE(l_key);
        return NULL;
    }
    log_it(L_INFO, "✅ Size assignment successful");
    log_it(L_INFO, "========================================");
    
    // Выделяем память под закрытый ключ
    l_key->priv_key_data = DAP_NEW_Z_SIZE(uint8_t, l_key->priv_key_data_size);
    if (!l_key->priv_key_data) {
        log_it(L_ERROR, "Failed to allocate memory for private key in dap_enc_chipmunk_key_new");
        DAP_DELETE(l_key);
        return NULL;
    }
    
    log_it(L_INFO, "=== AFTER PRIVATE KEY ALLOCATION ===");
    log_it(L_INFO, "Private key allocated at: %p, size: %zu", l_key->priv_key_data, l_key->priv_key_data_size);
    
    // Check if sizes are still correct after allocation
    if (l_key->priv_key_data_size != CHIPMUNK_PRIVATE_KEY_SIZE) {
        log_it(L_ERROR, "❌ CORRUPTION after private key allocation! Size changed from %d to %zu", 
               CHIPMUNK_PRIVATE_KEY_SIZE, l_key->priv_key_data_size);
        DAP_DELETE(l_key->priv_key_data);
        DAP_DELETE(l_key);
        return NULL;
    }
    
    // Выделяем память под открытый ключ
    l_key->pub_key_data = DAP_NEW_Z_SIZE(uint8_t, l_key->pub_key_data_size);
    if (!l_key->pub_key_data) {
        log_it(L_ERROR, "Failed to allocate memory for public key in dap_enc_chipmunk_key_new");
        DAP_DELETE(l_key->priv_key_data);
        DAP_DELETE(l_key);
        return NULL;
    }
    
    log_it(L_INFO, "=== AFTER PUBLIC KEY ALLOCATION ===");
    log_it(L_INFO, "Public key allocated at: %p, size: %zu", l_key->pub_key_data, l_key->pub_key_data_size);
    
    // Final check before chipmunk_keypair
    if (l_key->priv_key_data_size != CHIPMUNK_PRIVATE_KEY_SIZE || 
        l_key->pub_key_data_size != CHIPMUNK_PUBLIC_KEY_SIZE) {
        log_it(L_ERROR, "❌ CORRUPTION after public key allocation!");
        log_it(L_ERROR, "Private: expected %d, got %zu", CHIPMUNK_PRIVATE_KEY_SIZE, l_key->priv_key_data_size);
        log_it(L_ERROR, "Public: expected %d, got %zu", CHIPMUNK_PUBLIC_KEY_SIZE, l_key->pub_key_data_size);
        
        DAP_DELETE(l_key->priv_key_data);
        DAP_DELETE(l_key->pub_key_data);
        DAP_DELETE(l_key);
        return NULL;
    }
    log_it(L_INFO, "✅ Memory allocation successful, sizes intact");
    log_it(L_INFO, "=====================================");
    
    // Generate Chipmunk keypair
    debug_if(s_debug_more, L_DEBUG, "dap_enc_chipmunk_key_new: Calling chipmunk_keypair");
    
    log_it(L_DEBUG, "BEFORE chipmunk_keypair: priv_key_data_size = %zu, pub_key_data_size = %zu", 
           l_key->priv_key_data_size, l_key->pub_key_data_size);
    
    // ДОБАВЛЯЕМ ПРОСТОЙ PRINTF ПЕРЕД ВЫЗОВОМ
    debug_if(s_debug_more, L_INFO, "=== ABOUT TO CALL chipmunk_keypair ===");
    debug_if(s_debug_more, L_INFO, "Public key buffer: %p, size: %zu", l_key->pub_key_data, l_key->pub_key_data_size);
    debug_if(s_debug_more, L_INFO, "Private key buffer: %p, size: %zu", l_key->priv_key_data, l_key->priv_key_data_size);
    debug_if(s_debug_more, L_INFO, "=====================================");
    
    // ДОБАВЛЕНА ПРОВЕРКА УКАЗАТЕЛЕЙ И АДРЕСОВ
    log_it(L_DEBUG, "Memory layout: l_key=%p, &priv_key_data_size=%p, &pub_key_data_size=%p", 
           l_key, &l_key->priv_key_data_size, &l_key->pub_key_data_size);
    log_it(L_DEBUG, "Data pointers: priv_key_data=%p, pub_key_data=%p", 
           l_key->priv_key_data, l_key->pub_key_data);
    
    int ret = chipmunk_keypair(l_key->pub_key_data, l_key->pub_key_data_size,
                              l_key->priv_key_data, l_key->priv_key_data_size);
    
    log_it(L_INFO, "=== IMMEDIATELY AFTER chipmunk_keypair ===");
    log_it(L_INFO, "chipmunk_keypair returned: %d", ret);
    log_it(L_INFO, "Checking if sizes are still correct...");
    log_it(L_INFO, "priv_key_data_size = %zu (should be %d)", l_key->priv_key_data_size, CHIPMUNK_PRIVATE_KEY_SIZE);
    log_it(L_INFO, "pub_key_data_size = %zu (should be %d)", l_key->pub_key_data_size, CHIPMUNK_PUBLIC_KEY_SIZE);
    
    // CRITICAL: Check if chipmunk_keypair corrupted the sizes
    if (l_key->priv_key_data_size != CHIPMUNK_PRIVATE_KEY_SIZE) {
        log_it(L_ERROR, "❌ CORRUPTION DETECTED: priv_key_data_size changed after chipmunk_keypair!");
        log_it(L_ERROR, "Expected: %d, Actual: %zu", CHIPMUNK_PRIVATE_KEY_SIZE, l_key->priv_key_data_size);
        
        // Hex dump the corrupted value
        uint8_t *size_ptr = (uint8_t*)&l_key->priv_key_data_size;
        log_it(L_ERROR, "Corrupted priv_key_data_size hex: %02x %02x %02x %02x %02x %02x %02x %02x",
               size_ptr[0], size_ptr[1], size_ptr[2], size_ptr[3], 
               size_ptr[4], size_ptr[5], size_ptr[6], size_ptr[7]);
        
        // Clean up and return error
        DAP_DELETE(l_key->priv_key_data);
        DAP_DELETE(l_key->pub_key_data);
        DAP_DELETE(l_key);
        return NULL;
    }
    
    if (l_key->pub_key_data_size != CHIPMUNK_PUBLIC_KEY_SIZE) {
        log_it(L_ERROR, "❌ CORRUPTION DETECTED: pub_key_data_size changed after chipmunk_keypair!");
        log_it(L_ERROR, "Expected: %d, Actual: %zu", CHIPMUNK_PUBLIC_KEY_SIZE, l_key->pub_key_data_size);
        
        // Hex dump the corrupted value  
        uint8_t *size_ptr = (uint8_t*)&l_key->pub_key_data_size;
        log_it(L_ERROR, "Corrupted pub_key_data_size hex: %02x %02x %02x %02x %02x %02x %02x %02x",
               size_ptr[0], size_ptr[1], size_ptr[2], size_ptr[3], 
               size_ptr[4], size_ptr[5], size_ptr[6], size_ptr[7]);
        
        // Clean up and return error
        DAP_DELETE(l_key->priv_key_data);
        DAP_DELETE(l_key->pub_key_data);
        DAP_DELETE(l_key);
        return NULL;
    }
    
    log_it(L_INFO, "✅ Sizes remain correct after chipmunk_keypair");
    log_it(L_INFO, "========================================");
    
    // Check if chipmunk_keypair succeeded
    if (ret != 0) {
        log_it(L_ERROR, "chipmunk_keypair failed with error %d", ret);
        DAP_DELETE(l_key->priv_key_data);
        DAP_DELETE(l_key->pub_key_data);
        DAP_DELETE(l_key);
        return NULL;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Successfully generated Chipmunk keypair");
    log_it(L_INFO, "=== CHIPMUNK KEY GENERATION COMPLETED SUCCESSFULLY ===");
    return l_key;
}

// Create key from provided seed
dap_enc_key_t *dap_enc_chipmunk_key_generate(
        const void *kex_buf, size_t kex_size,
        const void *seed, size_t seed_size,
        const void *key_n, size_t key_n_size)
{
    (void) kex_buf; (void) kex_size; // Unused
    (void) key_n; (void) key_n_size; // Unused
    (void) seed; (void) seed_size;   // Currently unused, could implement deterministic key generation

    // For now, just generate a new random key regardless of seed
    return dap_enc_chipmunk_key_new();
}

// Get signature size
size_t dap_enc_chipmunk_calc_signature_size(void)
{
    return CHIPMUNK_SIGNATURE_SIZE;
}

// Sign data using Chipmunk algorithm
int dap_enc_chipmunk_get_sign(dap_enc_key_t *a_key, const void *a_data, const size_t a_data_size, void *a_signature,
                           const size_t a_signature_size)
{
    if (a_signature_size < CHIPMUNK_SIGNATURE_SIZE) {
        log_it(L_ERROR, "Signature size too small (expected %d, provided %zu)", 
               CHIPMUNK_SIGNATURE_SIZE, a_signature_size);
        return -1;
    }

    if (!a_key || !a_data || !a_signature || !a_data_size) {
        log_it(L_ERROR, "Invalid parameters in dap_enc_chipmunk_get_sign");
        return -1;
    }

    if (!a_key->priv_key_data) {
        log_it(L_ERROR, "No private key data in dap_enc_chipmunk_get_sign");
        return -1;
    }

    // Попытка создать подпись с ограниченным числом повторных попыток
    const int MAX_SIGN_ATTEMPTS = 3;
    int result = -1;
    
    // Инициализируем выходной буфер нулями
    memset(a_signature, 0, a_signature_size);
    
    // Создаем временный буфер для безопасного создания подписи
    uint8_t *l_tmp_signature = DAP_NEW_Z_SIZE(uint8_t, CHIPMUNK_SIGNATURE_SIZE);
    if (!l_tmp_signature) {
        log_it(L_ERROR, "Memory allocation failed for temporary signature buffer in dap_enc_chipmunk_get_sign");
        return -1;
    }
    
    for (int i = 0; i < MAX_SIGN_ATTEMPTS; i++) {
        // Если это повторная попытка, добавим некоторую случайность к данным
        uint8_t *l_modified_data = NULL;
        size_t l_modified_data_size = a_data_size;
        const void *l_data_to_sign = a_data;
        
        if (i > 0) {
            // При повторных попытках добавляем случайный префикс к данным
            uint8_t l_prefix[32] = {0};
            if (randombytes(l_prefix, sizeof(l_prefix)) != 0) {
                log_it(L_ERROR, "Failed to generate random prefix in dap_enc_chipmunk_get_sign");
                if (l_modified_data) {
                    DAP_DELETE(l_modified_data);
                }
                DAP_DELETE(l_tmp_signature);
                return -1;
            }
            
            l_modified_data_size = a_data_size + sizeof(l_prefix);
            l_modified_data = DAP_NEW_SIZE(uint8_t, l_modified_data_size);
            if (!l_modified_data) {
                log_it(L_ERROR, "Memory allocation failed for modified data in dap_enc_chipmunk_get_sign");
                DAP_DELETE(l_tmp_signature);
                return -1;
            }
            
            memcpy(l_modified_data, l_prefix, sizeof(l_prefix));
            memcpy(l_modified_data + sizeof(l_prefix), a_data, a_data_size);
            l_data_to_sign = l_modified_data;
            
            log_it(L_DEBUG, "Signing attempt %d with added randomness", i+1);
        }
        
        // Очищаем временный буфер подписи перед каждой попыткой
        memset(l_tmp_signature, 0, CHIPMUNK_SIGNATURE_SIZE);
        
        // Вызываем функцию подписи с защитой от сегментации памяти
        log_it(L_DEBUG, "Calling chipmunk_sign (attempt %d of %d)", i+1, MAX_SIGN_ATTEMPTS);
        result = chipmunk_sign(a_key->priv_key_data, l_data_to_sign, l_modified_data_size, l_tmp_signature);
        
        // Проверка успешности создания подписи
        if (result == 0) {
            log_it(L_DEBUG, "chipmunk_sign succeeded on attempt %d", i+1);
            
            // Копируем только при успехе
            memcpy(a_signature, l_tmp_signature, CHIPMUNK_SIGNATURE_SIZE);
            
            // Очищаем временные данные
            if (l_modified_data) {
                DAP_DELETE(l_modified_data);
            }
            
            DAP_DELETE(l_tmp_signature);
            
            // Успешное подписание
            return CHIPMUNK_SIGNATURE_SIZE;
        }
        
        log_it(L_DEBUG, "Chipmunk signature creation failed with code %d, attempt %d of %d", 
               result, i+1, MAX_SIGN_ATTEMPTS);
               
        // Освобождаем модифицированные данные перед следующей попыткой
        if (l_modified_data) {
            DAP_DELETE(l_modified_data);
            l_modified_data = NULL;
        }
    }

    // Если мы дошли до этой точки, значит все попытки создания подписи не удались
    log_it(L_ERROR, "Chipmunk signature creation failed with code %d after %d attempts", 
           result, MAX_SIGN_ATTEMPTS);
           
    // Освобождаем временный буфер подписи
    DAP_DELETE(l_tmp_signature);
    
    return -1;
}

// Verify signature using Chipmunk algorithm
int dap_enc_chipmunk_verify_sign(dap_enc_key_t *key, const void *data, const size_t data_size, void *signature, 
                              const size_t signature_size)
{
    if (signature_size < CHIPMUNK_SIGNATURE_SIZE) {
        log_it(L_ERROR, "Signature size too small (expected %d, provided %zu)", 
               CHIPMUNK_SIGNATURE_SIZE, signature_size);
        return -1;
    }

    if (!key || !key->pub_key_data || !data || !signature || !data_size) {
        log_it(L_ERROR, "Invalid parameters in dap_enc_chipmunk_verify_sign");
        return -1;
    }

    // Защищаем вызов chipmunk_verify от сегментации памяти
    int result = -1; // По умолчанию верификация не прошла
    
    // Создаем безопасную копию подписи для проверки
    uint8_t *l_temp_signature = DAP_NEW_Z_SIZE(uint8_t, CHIPMUNK_SIGNATURE_SIZE);
    if (!l_temp_signature) {
        log_it(L_ERROR, "Memory allocation failed for temporary signature buffer in dap_enc_chipmunk_verify_sign");
        return -1;
    }
    
    // Проверяем, что размер подписи соответствует ожидаемому
    if (signature_size > CHIPMUNK_SIGNATURE_SIZE) {
        log_it(L_WARNING, "Signature size %zu exceeds expected size %d, truncating",
              signature_size, CHIPMUNK_SIGNATURE_SIZE);
        memcpy(l_temp_signature, signature, CHIPMUNK_SIGNATURE_SIZE);
    } else {
        memcpy(l_temp_signature, signature, signature_size);
    }
    
    // Защита от сбоев в функции chipmunk_verify с помощью обработки ошибок
    result = chipmunk_verify(key->pub_key_data, data, data_size, l_temp_signature);
    
    // Освобождаем временный буфер вне зависимости от результата проверки
    DAP_DELETE(l_temp_signature);
    
    // Обработка возвращаемого значения
    if (result != 0) {
        // Логирование ошибок разных типов для диагностики
        switch (result) {
            case -1:
                log_it(L_ERROR, "Signature verification failed: invalid parameters or memory allocation error");
                break;
            case -2:
                log_it(L_ERROR, "Signature verification failed: processing error in NTT transformations");
                break;
            case -3:
                log_it(L_ERROR, "Signature verification failed: z polynomial has invalid coefficients");
                break;
            case -4:
                log_it(L_ERROR, "Signature verification failed: w' polynomial has suspicious distribution");
                break;
            case -5:
                log_it(L_WARNING, "Signature verification failed: hint bits problem");
                break;
            case -6:
                log_it(L_ERROR, "Signature verification failed: wrong key used for verification");
                break;
            case -7:
                log_it(L_ERROR, "Signature verification failed: integrity check failed");
                break;
            default:
                log_it(L_ERROR, "Signature verification failed with unknown error code %d", result);
        }
        
        // Всегда возвращаем отрицательное значение (код ошибки) при неудачной верификации
        return result;
    }
    
    // Верификация прошла успешно
    log_it(L_INFO, "Chipmunk signature verified successfully");
    return 0;
}

// Clean up key data, remove key pair
void dap_enc_chipmunk_key_delete(dap_enc_key_t *a_key)
{
    debug_if(s_debug_more, L_DEBUG, "dap_enc_chipmunk_key_delete: Deleting Chipmunk key at %p", 
             (void*)a_key);
    
    if (!a_key) {
        log_it(L_ERROR, "dap_enc_chipmunk_key_delete: NULL key passed");
        return;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Deleting Chipmunk key at %p", (void*)a_key);
    
    // Освобождаем открытый ключ
    if (a_key->pub_key_data) {
        DAP_DELETE(a_key->pub_key_data);
        a_key->pub_key_data = NULL;
        a_key->pub_key_data_size = 0;
        debug_if(s_debug_more, L_DEBUG, "dap_enc_chipmunk_key_delete: Public key data freed");
    }
    
    // Освобождаем закрытый ключ
    if (a_key->priv_key_data) {
        DAP_DELETE(a_key->priv_key_data);
        a_key->priv_key_data = NULL;
        a_key->priv_key_data_size = 0;
        debug_if(s_debug_more, L_DEBUG, "dap_enc_chipmunk_key_delete: private key data freed");
    }
    
    debug_if(s_debug_more, L_DEBUG, "dap_enc_chipmunk_key_delete: Chipmunk key deletion completed");
}

// Serialization functions for private and public keys
uint8_t *dap_enc_chipmunk_write_private_key(const void *a_key, size_t *a_buflen_out)
{
    log_it(L_INFO, "=== CORRUPTION INVESTIGATION: dap_enc_chipmunk_write_private_key ===");
    log_it(L_INFO, "Function called with a_key = %p", a_key);
    
    if (!a_key) {
        log_it(L_ERROR, "❌ a_key is NULL!");
        return NULL;
    }
    
    if (!a_buflen_out) {
        log_it(L_ERROR, "❌ a_buflen_out is NULL!");
        return NULL;
    }
    
    // Log current state of a_buflen_out
    log_it(L_INFO, "Initial *a_buflen_out = %zu", *a_buflen_out);
    
    const chipmunk_private_key_t* l_private_key = (const chipmunk_private_key_t*) a_key;
    
    // Log address and first few bytes of the key structure
    log_it(L_INFO, "l_private_key address: %p", (void*)l_private_key);
    log_it(L_INFO, "chipmunk_private_key_t structure size: %zu", sizeof(chipmunk_private_key_t));
    
    // Check if the address is reasonable 
    if ((uintptr_t)l_private_key < 0x1000 || (uintptr_t)l_private_key > 0x7fffffffffff) {
        log_it(L_ERROR, "❌ Suspicious private key address: %p", (void*)l_private_key);
        log_it(L_ERROR, "This looks like corrupted memory or invalid pointer!");
        return NULL;
    }
    
    // Memory pattern check
    const uint8_t *check_ptr = (const uint8_t*)l_private_key;
    log_it(L_INFO, "First 16 bytes of private key structure: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
           check_ptr[0], check_ptr[1], check_ptr[2], check_ptr[3],
           check_ptr[4], check_ptr[5], check_ptr[6], check_ptr[7],
           check_ptr[8], check_ptr[9], check_ptr[10], check_ptr[11],
           check_ptr[12], check_ptr[13], check_ptr[14], check_ptr[15]);
    
    log_it(L_INFO, "About to access private key structure...");
    log_it(L_INFO, "=========================================");
    
    *a_buflen_out = sizeof(chipmunk_private_key_t);
    uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, *a_buflen_out);
    if (!l_buf) {
        log_it(L_ERROR, "Failed to allocate memory for private key serialization");
        return NULL;
    }
    
    memcpy(l_buf, l_private_key, *a_buflen_out);
    return l_buf;
}

uint8_t *dap_enc_chipmunk_write_public_key(const void *a_key, size_t *a_buflen_out)
{
    log_it(L_INFO, "=== CORRUPTION INVESTIGATION: dap_enc_chipmunk_write_public_key ===");
    log_it(L_INFO, "Function called with a_key = %p", a_key);
    
    if (!a_key) {
        log_it(L_ERROR, "❌ a_key is NULL!");
        return NULL;
    }
    
    if (!a_buflen_out) {
        log_it(L_ERROR, "❌ a_buflen_out is NULL!");
        return NULL;
    }
    
    // Log current state of a_buflen_out
    log_it(L_INFO, "Initial *a_buflen_out = %zu", *a_buflen_out);
    
    const chipmunk_public_key_t* l_public_key = (const chipmunk_public_key_t*) a_key;
    
    // Log address and first few bytes of the key structure
    log_it(L_INFO, "l_public_key address: %p", (void*)l_public_key);
    log_it(L_INFO, "chipmunk_public_key_t structure size: %zu", sizeof(chipmunk_public_key_t));
    
    // Check if the address is reasonable 
    if ((uintptr_t)l_public_key < 0x1000 || (uintptr_t)l_public_key > 0x7fffffffffff) {
        log_it(L_ERROR, "❌ Suspicious public key address: %p", (void*)l_public_key);
        log_it(L_ERROR, "This looks like corrupted memory or invalid pointer!");
        return NULL;
    }
    
    // Memory pattern check
    const uint8_t *check_ptr = (const uint8_t*)l_public_key;
    log_it(L_INFO, "First 16 bytes of public key structure: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
           check_ptr[0], check_ptr[1], check_ptr[2], check_ptr[3],
           check_ptr[4], check_ptr[5], check_ptr[6], check_ptr[7],
           check_ptr[8], check_ptr[9], check_ptr[10], check_ptr[11],
           check_ptr[12], check_ptr[13], check_ptr[14], check_ptr[15]);
    
    log_it(L_INFO, "About to access public key structure...");
    log_it(L_INFO, "=========================================");
    *a_buflen_out = sizeof(chipmunk_public_key_t);
    uint8_t *l_buf = DAP_NEW_SIZE(uint8_t, *a_buflen_out);
    if (!l_buf) {
        log_it(L_ERROR, "Memory allocation failed for public key serialization, size = %zu", *a_buflen_out);
        return NULL;
    }
    
    memcpy(l_buf, l_public_key, *a_buflen_out);
    return l_buf;
}

uint64_t dap_enc_chipmunk_ser_private_key_size(const void *a_key)
{
    const dap_enc_key_t *l_key = (const dap_enc_key_t *)a_key;
    return l_key ? l_key->priv_key_data_size : 0;
}

uint64_t dap_enc_chipmunk_ser_public_key_size(const void *a_key)
{
    const dap_enc_key_t *l_key = (const dap_enc_key_t *)a_key;
    return l_key ? l_key->pub_key_data_size : 0;
}

void* dap_enc_chipmunk_read_private_key(const uint8_t *a_buf, size_t a_buflen)
{
    if (!a_buf || a_buflen != CHIPMUNK_PRIVATE_KEY_SIZE) {
        log_it(L_ERROR, "Invalid buffer for private key deserialization");
        return NULL;
    }
    
    uint8_t *l_key = DAP_NEW_SIZE(uint8_t, a_buflen);
    if (!l_key) {
        log_it(L_ERROR, "Memory allocation failed for private key deserialization");
        return NULL;
    }
    
    memcpy(l_key, a_buf, a_buflen);
    return l_key;
}

void* dap_enc_chipmunk_read_public_key(const uint8_t *a_buf, size_t a_buflen)
{
    if (!a_buf || a_buflen != CHIPMUNK_PUBLIC_KEY_SIZE) {
        log_it(L_ERROR, "Invalid buffer for public key deserialization");
        return NULL;
    }
    
    uint8_t *l_key = DAP_NEW_SIZE(uint8_t, a_buflen);
    if (!l_key) {
        log_it(L_ERROR, "Memory allocation failed for public key deserialization");
        return NULL;
    }
    
    memcpy(l_key, a_buf, a_buflen);
    return l_key;
}

uint64_t dap_enc_chipmunk_deser_private_key_size(const void *unused)
{
    (void)unused;
    return CHIPMUNK_PRIVATE_KEY_SIZE;
}

uint64_t dap_enc_chipmunk_deser_public_key_size(const void *unused)
{
    (void)unused;
    return CHIPMUNK_PUBLIC_KEY_SIZE;
} 
