/*
 * Unit tests for native ECDSA implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "dap_common.h"
#include "dap_sig_ecdsa.h"
#include "dap_rand.h"

// Test vectors from bitcoin-core/secp256k1

// Private key 1 -> known public key
static const uint8_t TEST_SECKEY_1[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};

// Expected pubkey for seckey=1 (which is the generator G)
// Gx = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
// Gy = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8
static const uint8_t EXPECTED_PUBKEY_1[65] = {
    0x04,  // Uncompressed prefix
    0x79, 0xBE, 0x66, 0x7E, 0xF9, 0xDC, 0xBB, 0xAC,
    0x55, 0xA0, 0x62, 0x95, 0xCE, 0x87, 0x0B, 0x07,
    0x02, 0x9B, 0xFC, 0xDB, 0x2D, 0xCE, 0x28, 0xD9,
    0x59, 0xF2, 0x81, 0x5B, 0x16, 0xF8, 0x17, 0x98,
    0x48, 0x3A, 0xDA, 0x77, 0x26, 0xA3, 0xC4, 0x65,
    0x5D, 0xA4, 0xFB, 0xFC, 0x0E, 0x11, 0x08, 0xA8,
    0xFD, 0x17, 0xB4, 0x48, 0xA6, 0x85, 0x54, 0x19,
    0x9C, 0x47, 0xD0, 0x8F, 0xFB, 0x10, 0xD4, 0xB8
};

// Private key 2
static const uint8_t TEST_SECKEY_2[32] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02
};

// Expected pubkey for seckey=2 (2*G)
// 2*G.x = 0xC6047F9441ED7D6D3045406E95C07CD85C778E4B8CEF3CA7ABAC09B95C709EE5
// 2*G.y = 0x1AE168FEA63DC339A3C58419466CEAEEF7F632653266D0E1236431A950CFE52A
static const uint8_t EXPECTED_PUBKEY_2[65] = {
    0x04,
    0xC6, 0x04, 0x7F, 0x94, 0x41, 0xED, 0x7D, 0x6D,
    0x30, 0x45, 0x40, 0x6E, 0x95, 0xC0, 0x7C, 0xD8,
    0x5C, 0x77, 0x8E, 0x4B, 0x8C, 0xEF, 0x3C, 0xA7,
    0xAB, 0xAC, 0x09, 0xB9, 0x5C, 0x70, 0x9E, 0xE5,
    0x1A, 0xE1, 0x68, 0xFE, 0xA6, 0x3D, 0xC3, 0x39,
    0xA3, 0xC5, 0x84, 0x19, 0x46, 0x6C, 0xEA, 0xE1,
    0x06, 0x1B, 0x7C, 0xD9, 0x88, 0xA6, 0xF7, 0xAD,
    0x2A, 0x1D, 0x8D, 0xB3, 0x8A, 0x3B, 0xFA, 0x46
};

static void print_hex(const char *label, const uint8_t *data, size_t len) {
    printf("%s: ", label);
    for (size_t i = 0; i < len; i++) {
        printf("%02X", data[i]);
    }
    printf("\n");
}

static bool test_pubkey_create(void) {
    printf("=== Test: Public Key Creation ===\n");
    
    dap_sig_ecdsa_pubkey_t pubkey;
    uint8_t serialized[65];
    size_t len;
    bool pass = true;
    
    // Test 1: seckey = 1 -> G
    printf("\nTest 1: seckey=1 -> G\n");
    if (!dap_sig_ecdsa_pubkey_create(NULL, &pubkey, TEST_SECKEY_1)) {
        printf("FAIL: pubkey_create returned false\n");
        return false;
    }
    
    len = sizeof(serialized);
    if (!dap_sig_ecdsa_pubkey_serialize(NULL, serialized, &len, &pubkey, DAP_SIG_ECDSA_EC_UNCOMPRESSED)) {
        printf("FAIL: pubkey_serialize returned false\n");
        return false;
    }
    
    print_hex("Expected", EXPECTED_PUBKEY_1, 65);
    print_hex("Got     ", serialized, 65);
    
    if (memcmp(serialized, EXPECTED_PUBKEY_1, 65) != 0) {
        printf("FAIL: Public key mismatch for seckey=1\n");
        pass = false;
    } else {
        printf("PASS\n");
    }
    
    // Test 2: seckey = 2 -> 2*G
    printf("\nTest 2: seckey=2 -> 2*G\n");
    if (!dap_sig_ecdsa_pubkey_create(NULL, &pubkey, TEST_SECKEY_2)) {
        printf("FAIL: pubkey_create returned false\n");
        return false;
    }
    
    len = sizeof(serialized);
    if (!dap_sig_ecdsa_pubkey_serialize(NULL, serialized, &len, &pubkey, DAP_SIG_ECDSA_EC_UNCOMPRESSED)) {
        printf("FAIL: pubkey_serialize returned false\n");
        return false;
    }
    
    print_hex("Expected", EXPECTED_PUBKEY_2, 65);
    print_hex("Got     ", serialized, 65);
    
    if (memcmp(serialized, EXPECTED_PUBKEY_2, 65) != 0) {
        printf("FAIL: Public key mismatch for seckey=2\n");
        pass = false;
    } else {
        printf("PASS\n");
    }
    
    return pass;
}

static bool test_sign_verify_basic(void) {
    printf("\n=== Test: Basic Sign/Verify ===\n");
    
    // Use known private key
    uint8_t seckey[32];
    randombytes(seckey, 32);
    
    // Ensure valid key
    while (!dap_sig_ecdsa_seckey_verify(NULL, seckey)) {
        randombytes(seckey, 32);
    }
    
    // Create public key
    dap_sig_ecdsa_pubkey_t pubkey;
    if (!dap_sig_ecdsa_pubkey_create(NULL, &pubkey, seckey)) {
        printf("FAIL: pubkey_create failed\n");
        return false;
    }
    
    // Message hash
    uint8_t msghash[32];
    randombytes(msghash, 32);
    
    // Sign
    dap_sig_ecdsa_signature_t sig;
    if (!dap_sig_ecdsa_sign(NULL, &sig, msghash, seckey, NULL, NULL)) {
        printf("FAIL: sign failed\n");
        return false;
    }
    
    print_hex("Message hash", msghash, 32);
    print_hex("Signature R ", sig.data, 32);
    print_hex("Signature S ", sig.data + 32, 32);
    
    // Verify
    if (!dap_sig_ecdsa_verify(NULL, &sig, msghash, &pubkey)) {
        printf("FAIL: verify returned false for valid signature\n");
        return false;
    }
    
    printf("PASS: Valid signature verified\n");
    
    // Test with wrong message
    uint8_t wrong_msg[32];
    memcpy(wrong_msg, msghash, 32);
    wrong_msg[0] ^= 0xFF;
    
    if (dap_sig_ecdsa_verify(NULL, &sig, wrong_msg, &pubkey)) {
        printf("FAIL: verify returned true for wrong message\n");
        return false;
    }
    
    printf("PASS: Wrong message rejected\n");
    
    return true;
}

static bool test_sign_verify_with_known_key(void) {
    printf("\n=== Test: Sign/Verify with seckey=1 ===\n");
    
    // Use seckey = 1
    dap_sig_ecdsa_pubkey_t pubkey;
    if (!dap_sig_ecdsa_pubkey_create(NULL, &pubkey, TEST_SECKEY_1)) {
        printf("FAIL: pubkey_create failed\n");
        return false;
    }
    
    // Simple message hash (all zeros)
    uint8_t msghash[32] = {0};
    msghash[31] = 0x01;  // hash = 1
    
    // Sign
    dap_sig_ecdsa_signature_t sig;
    if (!dap_sig_ecdsa_sign(NULL, &sig, msghash, TEST_SECKEY_1, NULL, NULL)) {
        printf("FAIL: sign failed\n");
        return false;
    }
    
    print_hex("Message hash", msghash, 32);
    print_hex("Signature R ", sig.data, 32);
    print_hex("Signature S ", sig.data + 32, 32);
    
    // Verify
    if (!dap_sig_ecdsa_verify(NULL, &sig, msghash, &pubkey)) {
        printf("FAIL: verify returned false for valid signature\n");
        return false;
    }
    
    printf("PASS: Signature verified\n");
    return true;
}

int main(void) {
    printf("====================================\n");
    printf("Native ECDSA Implementation Tests\n");
    printf("====================================\n");
    
    dap_common_init("test_ecdsa_native", NULL);
    
    int passed = 0, failed = 0;
    
    if (test_pubkey_create()) passed++; else failed++;
    if (test_sign_verify_with_known_key()) passed++; else failed++;
    if (test_sign_verify_basic()) passed++; else failed++;
    
    printf("\n====================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    printf("====================================\n");
    
    return failed > 0 ? 1 : 0;
}
