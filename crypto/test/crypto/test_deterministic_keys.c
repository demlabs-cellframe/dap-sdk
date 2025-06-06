#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dap_common.h"
#include "dap_enc_chipmunk.h"
#include "../../src/chipmunk/chipmunk.h"

#define LOG_TAG "test_deterministic"

int main() {
    // Initialize logging with clean format for unit tests
    dap_log_level_set(L_INFO);
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);  // Clean output without timestamps/modules
    
    // Initialize Chipmunk module
    dap_enc_chipmunk_init();
    
    log_it(L_NOTICE, "🔬 CHIPMUNK DETERMINISTIC KEY GENERATION TESTS");
    log_it(L_NOTICE, "Reproducible key generation from seeds");
    log_it(L_NOTICE, " ");
    
    if (dap_common_init("test_deterministic", NULL) != 0) {
        log_it(L_ERROR, "❌ DAP initialization failed");
        return 1;
    }
    
    if (chipmunk_init() != 0) {
        log_it(L_ERROR, "❌ Chipmunk initialization failed");
        return 1;
    }
    
    // Test seed
    uint8_t test_seed[32];
    for (int i = 0; i < 32; i++) {
        test_seed[i] = (uint8_t)(i + 1);  // 0x01, 0x02, ..., 0x20
    }
    
    // Generate two key pairs with the same seed
    uint8_t pub_key1[CHIPMUNK_PUBLIC_KEY_SIZE];
    uint8_t priv_key1[CHIPMUNK_PRIVATE_KEY_SIZE];
    uint8_t pub_key2[CHIPMUNK_PUBLIC_KEY_SIZE];
    uint8_t priv_key2[CHIPMUNK_PRIVATE_KEY_SIZE];
    
    log_it(L_INFO, "Generating first key pair from seed...");
    int ret1 = chipmunk_keypair_from_seed(test_seed,
                                          pub_key1, sizeof(pub_key1),
                                          priv_key1, sizeof(priv_key1));
    
    if (ret1 != 0) {
        log_it(L_ERROR, "❌ First key generation failed: %d", ret1);
        return 1;
    }
    
    log_it(L_INFO, "Generating second key pair from same seed...");
    int ret2 = chipmunk_keypair_from_seed(test_seed,
                                          pub_key2, sizeof(pub_key2),
                                          priv_key2, sizeof(priv_key2));
    
    if (ret2 != 0) {
        log_it(L_ERROR, "❌ Second key generation failed: %d", ret2);
        return 1;
    }
    
    // Compare public keys
    if (memcmp(pub_key1, pub_key2, CHIPMUNK_PUBLIC_KEY_SIZE) == 0) {
        log_it(L_NOTICE, "✅ Public keys are identical (deterministic generation works)");
    } else {
        log_it(L_ERROR, "❌ Public keys differ (deterministic generation failed)");
        return 1;
    }
    
    // Compare private keys
    if (memcmp(priv_key1, priv_key2, CHIPMUNK_PRIVATE_KEY_SIZE) == 0) {
        log_it(L_NOTICE, "✅ Private keys are identical (deterministic generation works)");
    } else {
        log_it(L_ERROR, "❌ Private keys differ (deterministic generation failed)");
        return 1;
    }
    
    // Test signing with both keys to ensure they work
    const char test_message[] = "Test message for deterministic keys";
    uint8_t signature1[CHIPMUNK_SIGNATURE_SIZE];
    uint8_t signature2[CHIPMUNK_SIGNATURE_SIZE];
    
    log_it(L_INFO, "Testing signing with both keys...");
    
    int sign1 = chipmunk_sign(priv_key1, (uint8_t*)test_message, strlen(test_message), signature1);
    int sign2 = chipmunk_sign(priv_key2, (uint8_t*)test_message, strlen(test_message), signature2);
    
    if (sign1 != 0 || sign2 != 0) {
        log_it(L_ERROR, "❌ Signing failed: %d, %d", sign1, sign2);
        return 1;
    }
    
    // Verify signatures
    int verify1 = chipmunk_verify(pub_key1, (uint8_t*)test_message, strlen(test_message), signature1);
    int verify2 = chipmunk_verify(pub_key2, (uint8_t*)test_message, strlen(test_message), signature2);
    
    if (verify1 != 0 || verify2 != 0) {
        log_it(L_ERROR, "❌ Verification failed: %d, %d", verify1, verify2);
        return 1;
    }
    
    log_it(L_NOTICE, "✅ Both keys can sign and verify successfully");
    
    // Test with different seed
    uint8_t different_seed[32];
    for (int i = 0; i < 32; i++) {
        different_seed[i] = (uint8_t)(i + 100);  // Different seed
    }
    
    uint8_t pub_key3[CHIPMUNK_PUBLIC_KEY_SIZE];
    uint8_t priv_key3[CHIPMUNK_PRIVATE_KEY_SIZE];
    
    log_it(L_INFO, "Testing with different seed...");
    int ret3 = chipmunk_keypair_from_seed(different_seed,
                                          pub_key3, sizeof(pub_key3),
                                          priv_key3, sizeof(priv_key3));
    
    if (ret3 != 0) {
        log_it(L_ERROR, "❌ Third key generation failed: %d", ret3);
        return 1;
    }
    
    // Keys should be different
    if (memcmp(pub_key1, pub_key3, CHIPMUNK_PUBLIC_KEY_SIZE) != 0) {
        log_it(L_NOTICE, "✅ Different seeds produce different keys (correct)");
    } else {
        log_it(L_ERROR, "❌ Different seeds produce same keys (incorrect)");
        return 1;
    }
    
    log_it(L_NOTICE, " ");
    log_it(L_NOTICE, "🎉 ALL DETERMINISTIC TESTS PASSED! ��");
    return 0;
} 