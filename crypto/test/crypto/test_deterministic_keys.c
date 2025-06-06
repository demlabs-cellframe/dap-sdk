#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dap_common.h"
#include "../../src/chipmunk/chipmunk.h"

#define LOG_TAG "test_deterministic"

int main() {
    printf("=== Testing Deterministic Key Generation ===\n");
    
    if (dap_common_init("test_deterministic", NULL) != 0) {
        printf("❌ DAP initialization failed\n");
        return 1;
    }
    
    if (chipmunk_init() != 0) {
        printf("❌ Chipmunk initialization failed\n");
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
    
    printf("Generating first key pair from seed...\n");
    int ret1 = chipmunk_keypair_from_seed(test_seed,
                                          pub_key1, sizeof(pub_key1),
                                          priv_key1, sizeof(priv_key1));
    
    if (ret1 != 0) {
        printf("❌ First key generation failed: %d\n", ret1);
        return 1;
    }
    
    printf("Generating second key pair from same seed...\n");
    int ret2 = chipmunk_keypair_from_seed(test_seed,
                                          pub_key2, sizeof(pub_key2),
                                          priv_key2, sizeof(priv_key2));
    
    if (ret2 != 0) {
        printf("❌ Second key generation failed: %d\n", ret2);
        return 1;
    }
    
    // Compare public keys
    if (memcmp(pub_key1, pub_key2, CHIPMUNK_PUBLIC_KEY_SIZE) == 0) {
        printf("✅ Public keys are identical (deterministic generation works)\n");
    } else {
        printf("❌ Public keys differ (deterministic generation failed)\n");
        return 1;
    }
    
    // Compare private keys
    if (memcmp(priv_key1, priv_key2, CHIPMUNK_PRIVATE_KEY_SIZE) == 0) {
        printf("✅ Private keys are identical (deterministic generation works)\n");
    } else {
        printf("❌ Private keys differ (deterministic generation failed)\n");
        return 1;
    }
    
    // Test signing with both keys to ensure they work
    const char test_message[] = "Test message for deterministic keys";
    uint8_t signature1[CHIPMUNK_SIGNATURE_SIZE];
    uint8_t signature2[CHIPMUNK_SIGNATURE_SIZE];
    
    printf("Testing signing with both keys...\n");
    
    int sign1 = chipmunk_sign(priv_key1, (uint8_t*)test_message, strlen(test_message), signature1);
    int sign2 = chipmunk_sign(priv_key2, (uint8_t*)test_message, strlen(test_message), signature2);
    
    if (sign1 != 0 || sign2 != 0) {
        printf("❌ Signing failed: %d, %d\n", sign1, sign2);
        return 1;
    }
    
    // Verify signatures
    int verify1 = chipmunk_verify(pub_key1, (uint8_t*)test_message, strlen(test_message), signature1);
    int verify2 = chipmunk_verify(pub_key2, (uint8_t*)test_message, strlen(test_message), signature2);
    
    if (verify1 != 0 || verify2 != 0) {
        printf("❌ Verification failed: %d, %d\n", verify1, verify2);
        return 1;
    }
    
    printf("✅ Both keys can sign and verify successfully\n");
    
    // Test with different seed
    uint8_t different_seed[32];
    for (int i = 0; i < 32; i++) {
        different_seed[i] = (uint8_t)(i + 100);  // Different seed
    }
    
    uint8_t pub_key3[CHIPMUNK_PUBLIC_KEY_SIZE];
    uint8_t priv_key3[CHIPMUNK_PRIVATE_KEY_SIZE];
    
    printf("Testing with different seed...\n");
    int ret3 = chipmunk_keypair_from_seed(different_seed,
                                          pub_key3, sizeof(pub_key3),
                                          priv_key3, sizeof(priv_key3));
    
    if (ret3 != 0) {
        printf("❌ Third key generation failed: %d\n", ret3);
        return 1;
    }
    
    // Keys should be different
    if (memcmp(pub_key1, pub_key3, CHIPMUNK_PUBLIC_KEY_SIZE) != 0) {
        printf("✅ Different seeds produce different keys (correct)\n");
    } else {
        printf("❌ Different seeds produce same keys (incorrect)\n");
        return 1;
    }
    
    printf("🎉 ALL DETERMINISTIC TESTS PASSED! 🎉\n");
    return 0;
} 