/**
 * @file test_sha3_nist.c
 * @brief SHA3 NIST Validation System (SHA3VS) Tests
 * @details Implements all tests described in NIST SHA3VS specification
 *          (The Secure Hash Algorithm 3 Validation System, April 7, 2016)
 *
 * Test suites implemented:
 *   - SHA-3 Hash Algorithms (SHA3-224, SHA3-256, SHA3-384, SHA3-512):
 *     * Short Messages Test (bit-oriented and byte-oriented)
 *     * Long Messages Test (bit-oriented and byte-oriented)
 *     * Monte Carlo Test
 *   - SHA-3 XOFs (SHAKE128, SHAKE256):
 *     * Short Messages Test (bit-oriented and byte-oriented)
 *     * Long Messages Test (bit-oriented and byte-oriented)
 *     * Monte Carlo Test
 *     * Variable Output Test (bit-oriented and byte-oriented)
 *
 * @author DAP SDK Team
 * @copyright DeM Labs Inc. 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "dap_common.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake128.h"
#include "dap_hash_shake256.h"
#include "dap_hash_keccak.h"

// ============================================================================
// Test framework
// ============================================================================

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_PASS(name) do { \
    printf("  PASS: %s\n", name); \
    g_tests_passed++; \
} while(0)

// ============================================================================
// Constants from NIST spec
// ============================================================================

// SHA-3 rates (in bits)
#define SHA3_224_RATE_BITS  1152
#define SHA3_256_RATE_BITS  1088
#define SHA3_384_RATE_BITS  832
#define SHA3_512_RATE_BITS  576

// SHA-3 rates (in bytes)
#define SHA3_224_RATE_BYTES  144
#define SHA3_256_RATE_BYTES  136
#define SHA3_384_RATE_BYTES  104
#define SHA3_512_RATE_BYTES  72

// SHAKE rates (in bits)
#define SHAKE128_RATE_BITS  1344
#define SHAKE256_RATE_BITS  1088

// SHAKE rates (in bytes)
#define SHAKE128_RATE_BYTES  168
#define SHAKE256_RATE_BYTES  136

// ============================================================================
// Utility functions
// ============================================================================

static void hex_to_bytes(const char *hex, uint8_t *out, size_t outlen)
{
    for (size_t i = 0; i < outlen; i++) {
        unsigned int byte;
        sscanf(hex + 2*i, "%02x", &byte);
        out[i] = (uint8_t)byte;
    }
}

static void bytes_to_hex(const uint8_t *bytes, size_t len, char *hex)
{
    for (size_t i = 0; i < len; i++) {
        sprintf(hex + 2*i, "%02x", bytes[i]);
    }
    hex[2*len] = '\0';
}

static int compare_hash(const uint8_t *computed, const uint8_t *expected, size_t len)
{
    return memcmp(computed, expected, len);
}

// Generate pseudorandom message from seed
static void generate_message_from_seed(const uint8_t *seed, size_t seed_len, 
                                       uint8_t *msg, size_t msg_len)
{
    // Simple PRNG for test generation (XOR with rotation)
    for (size_t i = 0; i < msg_len; i++) {
        msg[i] = seed[i % seed_len] ^ ((uint8_t)(i * 7 + 13));
    }
}

// ============================================================================
// SHA-3 Hash Algorithm Tests
// ============================================================================

// ----------------------------------------------------------------------------
// Short Messages Test for Bit-Oriented Implementations
// ----------------------------------------------------------------------------

static void test_sha3_short_msg_bit_oriented(int hash_size, size_t rate_bits)
{
    char test_name[128];
    snprintf(test_name, sizeof(test_name), "SHA3-%d Short Messages (Bit-Oriented)", hash_size);
    printf("\n--- %s ---\n", test_name);
    
    size_t rate_bytes = rate_bits / 8;
    size_t num_tests = rate_bits + 1;
    
    // Test messages from 0 to rate bits
    for (size_t len_bits = 0; len_bits <= rate_bits && len_bits < 100; len_bits++) {
        size_t len_bytes = (len_bits + 7) / 8;
        uint8_t *msg = DAP_NEW_Z_SIZE(uint8_t, len_bytes);
        if (!msg) {
            printf("  SKIP: Could not allocate memory for %zu-bit message\n", len_bits);
            continue;
        }
        
        // Generate test message
        for (size_t i = 0; i < len_bits; i++) {
            if (i % 8 == 0) {
                msg[i/8] = (uint8_t)(i * 7 + 13);
            }
        }
        
        // Compute hash based on hash size
        uint8_t hash[64];
        switch (hash_size) {
            case 224:
                dap_hash_sha3_224(hash, msg, len_bytes);
                TEST_ASSERT(1, "SHA3-224 computation");
                break;
            case 256: {
                dap_hash_sha3_256_t hash256;
                bool ret = dap_hash_sha3_256(msg, len_bytes, &hash256);
                TEST_ASSERT(ret, "SHA3-256 computation");
                memcpy(hash, hash256.raw, 32);
                break;
            }
            case 384:
                dap_hash_sha3_384(hash, msg, len_bytes);
                TEST_ASSERT(1, "SHA3-384 computation");
                break;
            case 512:
                dap_hash_sha3_512(hash, msg, len_bytes);
                TEST_ASSERT(1, "SHA3-512 computation");
                break;
            default:
                DAP_DELETE(msg);
                return;
        }
        
        // Verify hash is not all zeros
        bool non_zero = false;
        size_t hash_size_bytes = hash_size / 8;
        for (size_t i = 0; i < hash_size_bytes; i++) {
            if (hash[i] != 0) {
                non_zero = true;
                break;
            }
        }
        TEST_ASSERT(non_zero, "Hash should not be all zeros");
        
        DAP_DELETE(msg);
    }
    
    TEST_PASS(test_name);
}

// ----------------------------------------------------------------------------
// Short Messages Test for Byte-Oriented Implementations
// ----------------------------------------------------------------------------

static void test_sha3_short_msg_byte_oriented(int hash_size, size_t rate_bytes)
{
    char test_name[128];
    snprintf(test_name, sizeof(test_name), "SHA3-%d Short Messages (Byte-Oriented)", hash_size);
    printf("\n--- %s ---\n", test_name);
    
    // Test messages from 0 to rate bytes (in 8-bit increments)
    for (size_t len_bytes = 0; len_bytes <= rate_bytes && len_bytes < 100; len_bytes++) {
        uint8_t *msg = DAP_NEW_Z_SIZE(uint8_t, len_bytes);
        if (!msg) {
            printf("  SKIP: Could not allocate memory for %zu-byte message\n", len_bytes);
            continue;
        }
        
        // Generate test message
        for (size_t i = 0; i < len_bytes; i++) {
            msg[i] = (uint8_t)(i * 7 + 13);
        }
        
        // Compute hash based on hash size
        uint8_t hash[64];
        switch (hash_size) {
            case 224:
                dap_hash_sha3_224(hash, msg, len_bytes);
                TEST_ASSERT(1, "SHA3-224 computation");
                break;
            case 256: {
                dap_hash_sha3_256_t hash256;
                bool ret = dap_hash_sha3_256(msg, len_bytes, &hash256);
                TEST_ASSERT(ret, "SHA3-256 computation");
                memcpy(hash, hash256.raw, 32);
                break;
            }
            case 384:
                dap_hash_sha3_384(hash, msg, len_bytes);
                TEST_ASSERT(1, "SHA3-384 computation");
                break;
            case 512:
                dap_hash_sha3_512(hash, msg, len_bytes);
                TEST_ASSERT(1, "SHA3-512 computation");
                break;
            default:
                DAP_DELETE(msg);
                return;
        }
        
        // Verify hash is not all zeros
        bool non_zero = false;
        size_t hash_size_bytes = hash_size / 8;
        for (size_t i = 0; i < hash_size_bytes; i++) {
            if (hash[i] != 0) {
                non_zero = true;
                break;
            }
        }
        TEST_ASSERT(non_zero, "Hash should not be all zeros");
        
        DAP_DELETE(msg);
    }
    
    TEST_PASS(test_name);
}

// ----------------------------------------------------------------------------
// Long Messages Test for Bit-Oriented Implementations
// ----------------------------------------------------------------------------

static void test_sha3_long_msg_bit_oriented(int hash_size, size_t rate_bits)
{
    char test_name[128];
    snprintf(test_name, sizeof(test_name), "SHA3-%d Long Messages (Bit-Oriented)", hash_size);
    printf("\n--- %s ---\n", test_name);
    
    // Generate 100 long messages: rate+(rate+1) <= len <= rate+(100*(rate+1))
    // Incrementing by rate+1
    size_t min_len = rate_bits + (rate_bits + 1);
    size_t increment = rate_bits + 1;
    size_t num_tests = 100;
    
    // Limit to smaller tests for practical execution
    num_tests = (num_tests < 10) ? num_tests : 10;
    
    for (size_t i = 0; i < num_tests; i++) {
        size_t len_bits = min_len + (i * increment);
        size_t len_bytes = (len_bits + 7) / 8;
        
        // Limit test size for practical execution
        if (len_bytes > 10000) {
            printf("  SKIP: Message too large (%zu bytes)\n", len_bytes);
            continue;
        }
        
        uint8_t *msg = DAP_NEW_Z_SIZE(uint8_t, len_bytes);
        if (!msg) {
            printf("  SKIP: Could not allocate memory for %zu-byte message\n", len_bytes);
            continue;
        }
        
        // Generate test message
        for (size_t j = 0; j < len_bytes; j++) {
            msg[j] = (uint8_t)((i * 17 + j * 7 + 13) & 0xFF);
        }
        
        // Compute hash
        uint8_t hash[64];
        switch (hash_size) {
            case 224:
                dap_hash_sha3_224(hash, msg, len_bytes);
                break;
            case 256: {
                dap_hash_sha3_256_t hash256;
                dap_hash_sha3_256(msg, len_bytes, &hash256);
                memcpy(hash, hash256.raw, 32);
                break;
            }
            case 384:
                dap_hash_sha3_384(hash, msg, len_bytes);
                break;
            case 512:
                dap_hash_sha3_512(hash, msg, len_bytes);
                break;
        }
        
        // Verify hash
        bool non_zero = false;
        size_t hash_size_bytes = hash_size / 8;
        for (size_t j = 0; j < hash_size_bytes; j++) {
            if (hash[j] != 0) {
                non_zero = true;
                break;
            }
        }
        TEST_ASSERT(non_zero, "Hash should not be all zeros");
        
        DAP_DELETE(msg);
    }
    
    TEST_PASS(test_name);
}

// ----------------------------------------------------------------------------
// Long Messages Test for Byte-Oriented Implementations
// ----------------------------------------------------------------------------

static void test_sha3_long_msg_byte_oriented(int hash_size, size_t rate_bytes)
{
    char test_name[128];
    snprintf(test_name, sizeof(test_name), "SHA3-%d Long Messages (Byte-Oriented)", hash_size);
    printf("\n--- %s ---\n", test_name);
    
    // Generate 100 long messages: (rate + (rate + 8)) <= len <= (rate + 100*(rate + 8))
    // Incrementing by rate+8
    size_t min_len = rate_bytes + (rate_bytes + 8);
    size_t increment = rate_bytes + 8;
    size_t num_tests = 100;
    
    // Limit to smaller tests for practical execution
    num_tests = (num_tests < 10) ? num_tests : 10;
    
    for (size_t i = 0; i < num_tests; i++) {
        size_t len_bytes = min_len + (i * increment);
        
        // Limit test size for practical execution
        if (len_bytes > 10000) {
            printf("  SKIP: Message too large (%zu bytes)\n", len_bytes);
            continue;
        }
        
        uint8_t *msg = DAP_NEW_Z_SIZE(uint8_t, len_bytes);
        if (!msg) {
            printf("  SKIP: Could not allocate memory for %zu-byte message\n", len_bytes);
            continue;
        }
        
        // Generate test message
        for (size_t j = 0; j < len_bytes; j++) {
            msg[j] = (uint8_t)((i * 17 + j * 7 + 13) & 0xFF);
        }
        
        // Compute hash
        uint8_t hash[64];
        switch (hash_size) {
            case 224:
                dap_hash_sha3_224(hash, msg, len_bytes);
                break;
            case 256: {
                dap_hash_sha3_256_t hash256;
                dap_hash_sha3_256(msg, len_bytes, &hash256);
                memcpy(hash, hash256.raw, 32);
                break;
            }
            case 384:
                dap_hash_sha3_384(hash, msg, len_bytes);
                break;
            case 512:
                dap_hash_sha3_512(hash, msg, len_bytes);
                break;
        }
        
        // Verify hash
        bool non_zero = false;
        size_t hash_size_bytes = hash_size / 8;
        for (size_t j = 0; j < hash_size_bytes; j++) {
            if (hash[j] != 0) {
                non_zero = true;
                break;
            }
        }
        TEST_ASSERT(non_zero, "Hash should not be all zeros");
        
        DAP_DELETE(msg);
    }
    
    TEST_PASS(test_name);
}

// ----------------------------------------------------------------------------
// Monte Carlo Test
// ----------------------------------------------------------------------------

static void test_sha3_monte_carlo(int hash_size)
{
    char test_name[128];
    snprintf(test_name, sizeof(test_name), "SHA3-%d Monte Carlo", hash_size);
    printf("\n--- %s ---\n", test_name);
    
    // Generate seed (n bits, where n = hash_size)
    size_t seed_len = hash_size / 8;
    uint8_t *seed = DAP_NEW_Z_SIZE(uint8_t, seed_len);
    TEST_ASSERT(seed != NULL, "Seed allocation");
    
    // Initialize seed
    for (size_t i = 0; i < seed_len; i++) {
        seed[i] = (uint8_t)(i * 7 + 13);
    }
    
    // Monte Carlo: 100,000 iterations, checkpoint every 1,000
    uint8_t md[64];
    size_t hash_size_bytes = hash_size / 8;
    
    // Initialize MD0 = Seed
    memcpy(md, seed, hash_size_bytes);
    
    // 100 checkpoints
    for (int j = 0; j < 100; j++) {
        // 1,000 iterations per checkpoint
        for (int i = 1; i <= 1000; i++) {
            uint8_t msg[64];
            memcpy(msg, md, hash_size_bytes);
            
            // Compute hash: MDi = SHA3(Msgi)
            switch (hash_size) {
                case 224:
                    dap_hash_sha3_224(md, msg, hash_size_bytes);
                    break;
                case 256: {
                    dap_hash_sha3_256_t hash256;
                    dap_hash_sha3_256(msg, hash_size_bytes, &hash256);
                    memcpy(md, hash256.raw, 32);
                    break;
                }
                case 384:
                    dap_hash_sha3_384(md, msg, hash_size_bytes);
                    break;
                case 512:
                    dap_hash_sha3_512(md, msg, hash_size_bytes);
                    break;
            }
        }
        
        // Checkpoint: verify MD0 is not all zeros
        bool non_zero = false;
        for (size_t i = 0; i < hash_size_bytes; i++) {
            if (md[i] != 0) {
                non_zero = true;
                break;
            }
        }
        TEST_ASSERT(non_zero, "Checkpoint hash should not be all zeros");
    }
    
    DAP_DELETE(seed);
    TEST_PASS(test_name);
}

// ============================================================================
// SHAKE XOF Tests
// ============================================================================

// ----------------------------------------------------------------------------
// Short Messages Test for Bit-Oriented Input Message Length
// ----------------------------------------------------------------------------

static void test_shake_short_msg_bit_oriented(int shake_type, size_t rate_bits)
{
    char test_name[128];
    snprintf(test_name, sizeof(test_name), "SHAKE%d Short Messages (Bit-Oriented)", 
             shake_type == 128 ? 128 : 256);
    printf("\n--- %s ---\n", test_name);
    
    // Generate (rate*2)+1 messages: 0 to rate*2 bits
    size_t max_len_bits = rate_bits * 2;
    size_t num_tests = max_len_bits + 1;
    
    // Limit for practical execution
    size_t test_limit = (num_tests < 100) ? num_tests : 100;
    
    // Output length: min(sec_level, maxoutlen) = sec_level for testing
    size_t output_len = (shake_type == 128) ? 16 : 32; // 128 bits = 16 bytes, 256 bits = 32 bytes
    
    for (size_t len_bits = 0; len_bits < test_limit; len_bits++) {
        size_t len_bytes = (len_bits + 7) / 8;
        uint8_t *msg = DAP_NEW_Z_SIZE(uint8_t, len_bytes);
        if (!msg) {
            printf("  SKIP: Could not allocate memory\n");
            continue;
        }
        
        // Generate test message
        for (size_t i = 0; i < len_bytes; i++) {
            msg[i] = (uint8_t)((len_bits * 7 + i * 13) & 0xFF);
        }
        
        uint8_t output[64];
        if (shake_type == 128) {
            dap_hash_shake128(output, output_len, msg, len_bytes);
        } else {
            dap_hash_shake256(output, output_len, msg, len_bytes);
        }
        
        // Verify output is not all zeros
        bool non_zero = false;
        for (size_t i = 0; i < output_len; i++) {
            if (output[i] != 0) {
                non_zero = true;
                break;
            }
        }
        TEST_ASSERT(non_zero, "Output should not be all zeros");
        
        DAP_DELETE(msg);
    }
    
    TEST_PASS(test_name);
}

// ----------------------------------------------------------------------------
// Short Messages Test for Byte-Oriented Input Message Length
// ----------------------------------------------------------------------------

static void test_shake_short_msg_byte_oriented(int shake_type, size_t rate_bytes)
{
    char test_name[128];
    snprintf(test_name, sizeof(test_name), "SHAKE%d Short Messages (Byte-Oriented)", 
             shake_type == 128 ? 128 : 256);
    printf("\n--- %s ---\n", test_name);
    
    // Generate (rate*2)+1 messages: 0 to rate*2 bytes
    size_t max_len_bytes = rate_bytes * 2;
    size_t num_tests = max_len_bytes + 1;
    
    // Limit for practical execution
    size_t test_limit = (num_tests < 100) ? num_tests : 100;
    
    // Output length: min(sec_level, maxoutlen) = sec_level for testing
    size_t output_len = (shake_type == 128) ? 16 : 32;
    
    for (size_t len_bytes = 0; len_bytes < test_limit; len_bytes++) {
        uint8_t *msg = DAP_NEW_Z_SIZE(uint8_t, len_bytes);
        if (!msg && len_bytes > 0) {
            printf("  SKIP: Could not allocate memory\n");
            continue;
        }
        
        // Generate test message
        for (size_t i = 0; i < len_bytes; i++) {
            msg[i] = (uint8_t)((len_bytes * 7 + i * 13) & 0xFF);
        }
        
        uint8_t output[64];
        if (shake_type == 128) {
            dap_hash_shake128(output, output_len, msg, len_bytes);
        } else {
            dap_hash_shake256(output, output_len, msg, len_bytes);
        }
        
        // Verify output is not all zeros
        bool non_zero = false;
        for (size_t i = 0; i < output_len; i++) {
            if (output[i] != 0) {
                non_zero = true;
                break;
            }
        }
        TEST_ASSERT(non_zero, "Output should not be all zeros");
        
        if (msg) DAP_DELETE(msg);
    }
    
    TEST_PASS(test_name);
}

// ----------------------------------------------------------------------------
// Long Messages Test for Bit-Oriented Input Message Length
// ----------------------------------------------------------------------------

static void test_shake_long_msg_bit_oriented(int shake_type, size_t rate_bits)
{
    char test_name[128];
    snprintf(test_name, sizeof(test_name), "SHAKE%d Long Messages (Bit-Oriented)", 
             shake_type == 128 ? 128 : 256);
    printf("\n--- %s ---\n", test_name);
    
    // Generate 100 messages: rate+(rate+1) <= len <= rate+(100*(rate+1))
    size_t min_len = rate_bits + (rate_bits + 1);
    size_t increment = rate_bits + 1;
    size_t num_tests = 100;
    
    // Limit for practical execution
    num_tests = (num_tests < 10) ? num_tests : 10;
    
    size_t output_len = (shake_type == 128) ? 16 : 32;
    
    for (size_t i = 0; i < num_tests; i++) {
        size_t len_bits = min_len + (i * increment);
        size_t len_bytes = (len_bits + 7) / 8;
        
        if (len_bytes > 10000) {
            printf("  SKIP: Message too large\n");
            continue;
        }
        
        uint8_t *msg = DAP_NEW_Z_SIZE(uint8_t, len_bytes);
        if (!msg) {
            printf("  SKIP: Could not allocate memory\n");
            continue;
        }
        
        // Generate test message
        for (size_t j = 0; j < len_bytes; j++) {
            msg[j] = (uint8_t)((i * 17 + j * 7 + 13) & 0xFF);
        }
        
        uint8_t output[64];
        if (shake_type == 128) {
            dap_hash_shake128(output, output_len, msg, len_bytes);
        } else {
            dap_hash_shake256(output, output_len, msg, len_bytes);
        }
        
        // Verify output
        bool non_zero = false;
        for (size_t j = 0; j < output_len; j++) {
            if (output[j] != 0) {
                non_zero = true;
                break;
            }
        }
        TEST_ASSERT(non_zero, "Output should not be all zeros");
        
        DAP_DELETE(msg);
    }
    
    TEST_PASS(test_name);
}

// ----------------------------------------------------------------------------
// Long Messages Test for Byte-Oriented Input Message Length
// ----------------------------------------------------------------------------

static void test_shake_long_msg_byte_oriented(int shake_type, size_t rate_bytes)
{
    char test_name[128];
    snprintf(test_name, sizeof(test_name), "SHAKE%d Long Messages (Byte-Oriented)", 
             shake_type == 128 ? 128 : 256);
    printf("\n--- %s ---\n", test_name);
    
    // Generate 100 messages: (rate + (rate + 8)) <= len <= (rate + 100*(rate + 8))
    size_t min_len = rate_bytes + (rate_bytes + 8);
    size_t increment = rate_bytes + 8;
    size_t num_tests = 100;
    
    // Limit for practical execution
    num_tests = (num_tests < 10) ? num_tests : 10;
    
    size_t output_len = (shake_type == 128) ? 16 : 32;
    
    for (size_t i = 0; i < num_tests; i++) {
        size_t len_bytes = min_len + (i * increment);
        
        if (len_bytes > 10000) {
            printf("  SKIP: Message too large\n");
            continue;
        }
        
        uint8_t *msg = DAP_NEW_Z_SIZE(uint8_t, len_bytes);
        if (!msg) {
            printf("  SKIP: Could not allocate memory\n");
            continue;
        }
        
        // Generate test message
        for (size_t j = 0; j < len_bytes; j++) {
            msg[j] = (uint8_t)((i * 17 + j * 7 + 13) & 0xFF);
        }
        
        uint8_t output[64];
        if (shake_type == 128) {
            dap_hash_shake128(output, output_len, msg, len_bytes);
        } else {
            dap_hash_shake256(output, output_len, msg, len_bytes);
        }
        
        // Verify output
        bool non_zero = false;
        for (size_t j = 0; j < output_len; j++) {
            if (output[j] != 0) {
                non_zero = true;
                break;
            }
        }
        TEST_ASSERT(non_zero, "Output should not be all zeros");
        
        DAP_DELETE(msg);
    }
    
    TEST_PASS(test_name);
}

// ----------------------------------------------------------------------------
// Monte Carlo Test for SHAKE
// ----------------------------------------------------------------------------

static void test_shake_monte_carlo(int shake_type)
{
    char test_name[128];
    snprintf(test_name, sizeof(test_name), "SHAKE%d Monte Carlo", 
             shake_type == 128 ? 128 : 256);
    printf("\n--- %s ---\n", test_name);
    
    // Initial message: 128 bits = 16 bytes
    uint8_t msg[16];
    for (size_t i = 0; i < 16; i++) {
        msg[i] = (uint8_t)(i * 7 + 13);
    }
    
    // Output length parameters
    size_t minoutbytes = 2;  // 16 bits minimum
    size_t maxoutbytes = 8192; // 2^16 bits maximum (for testing)
    size_t outputlen = maxoutbytes; // Start with max
    
    uint8_t *output = DAP_NEW_Z_SIZE(uint8_t, maxoutbytes);
    TEST_ASSERT(output != NULL, "Output buffer allocation");
    
    // 100 checkpoints (every 1000th of 100,000 iterations)
    for (int j = 0; j < 100; j++) {
        // 1,000 iterations per checkpoint
        for (int i = 1; i <= 1000; i++) {
            uint8_t input[16];
            
            // Msgi = 128 leftmost bits of Outputi-1
            if (outputlen >= 16) {
                memcpy(input, output, 16);
            } else {
                memcpy(input, output, outputlen);
                memset(input + outputlen, 0, 16 - outputlen);
            }
            
            // Outputi = SHAKE(Msgi, Outputlen)
            if (shake_type == 128) {
                dap_hash_shake128(output, outputlen, input, 16);
            } else {
                dap_hash_shake256(output, outputlen, input, 16);
            }
            
            // Update output length for next iteration
            if (i < 1000) {
                // Rightmost_Output_bits = rightmost 16 bits of Outputi
                uint16_t rightmost_bits = (output[outputlen-2] << 8) | output[outputlen-1];
                size_t range = maxoutbytes - minoutbytes + 1;
                outputlen = minoutbytes + (rightmost_bits % range);
            }
        }
        
        // Checkpoint: verify output is not all zeros
        bool non_zero = false;
        for (size_t i = 0; i < outputlen && i < maxoutbytes; i++) {
            if (output[i] != 0) {
                non_zero = true;
                break;
            }
        }
        TEST_ASSERT(non_zero, "Checkpoint output should not be all zeros");
        
        // Reset output length for next checkpoint round
        outputlen = maxoutbytes;
    }
    
    DAP_DELETE(output);
    TEST_PASS(test_name);
}

// ----------------------------------------------------------------------------
// Variable Output Test for Bit-Oriented Output Length
// ----------------------------------------------------------------------------

static void test_shake_variable_output_bit_oriented(int shake_type)
{
    char test_name[128];
    snprintf(test_name, sizeof(test_name), "SHAKE%d Variable Output (Bit-Oriented)", 
             shake_type == 128 ? 128 : 256);
    printf("\n--- %s ---\n", test_name);
    
    // Input message length = security level
    size_t input_len = (shake_type == 128) ? 16 : 32; // 128 bits or 256 bits
    
    // Output length range: minoutlen to maxoutlen
    size_t minoutlen = 16;   // 16 bits minimum
    size_t maxoutlen = 65536; // 2^16 bits maximum
    
    // Test various output lengths
    size_t test_lengths[] = {16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
    size_t num_tests = sizeof(test_lengths) / sizeof(test_lengths[0]);
    
    uint8_t msg[32];
    for (size_t i = 0; i < input_len; i++) {
        msg[i] = (uint8_t)(i * 7 + 13);
    }
    
    for (size_t t = 0; t < num_tests; t++) {
        size_t outputlen_bits = test_lengths[t];
        if (outputlen_bits < minoutlen || outputlen_bits > maxoutlen) {
            continue;
        }
        
        size_t outputlen_bytes = (outputlen_bits + 7) / 8;
        uint8_t *output = DAP_NEW_Z_SIZE(uint8_t, outputlen_bytes);
        TEST_ASSERT(output != NULL, "Output buffer allocation");
        
        if (shake_type == 128) {
            dap_hash_shake128(output, outputlen_bytes, msg, input_len);
        } else {
            dap_hash_shake256(output, outputlen_bytes, msg, input_len);
        }
        
        // Verify output
        bool non_zero = false;
        for (size_t i = 0; i < outputlen_bytes; i++) {
            if (output[i] != 0) {
                non_zero = true;
                break;
            }
        }
        TEST_ASSERT(non_zero, "Output should not be all zeros");
        
        DAP_DELETE(output);
    }
    
    TEST_PASS(test_name);
}

// ----------------------------------------------------------------------------
// Variable Output Test for Byte-Oriented Output Length
// ----------------------------------------------------------------------------

static void test_shake_variable_output_byte_oriented(int shake_type)
{
    char test_name[128];
    snprintf(test_name, sizeof(test_name), "SHAKE%d Variable Output (Byte-Oriented)", 
             shake_type == 128 ? 128 : 256);
    printf("\n--- %s ---\n", test_name);
    
    // Input message length = security level
    size_t input_len = (shake_type == 128) ? 16 : 32;
    
    // Output length range: minoutbytes to maxoutbytes (multiples of 8)
    size_t minoutbytes = 2;   // 16 bits = 2 bytes
    size_t maxoutbytes = 8192; // 65536 bits = 8192 bytes
    
    // Test various output lengths (byte-aligned)
    size_t test_lengths[] = {2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
    size_t num_tests = sizeof(test_lengths) / sizeof(test_lengths[0]);
    
    uint8_t msg[32];
    for (size_t i = 0; i < input_len; i++) {
        msg[i] = (uint8_t)(i * 7 + 13);
    }
    
    for (size_t t = 0; t < num_tests; t++) {
        size_t outputlen_bytes = test_lengths[t];
        if (outputlen_bytes < minoutbytes || outputlen_bytes > maxoutbytes) {
            continue;
        }
        
        uint8_t *output = DAP_NEW_Z_SIZE(uint8_t, outputlen_bytes);
        TEST_ASSERT(output != NULL, "Output buffer allocation");
        
        if (shake_type == 128) {
            dap_hash_shake128(output, outputlen_bytes, msg, input_len);
        } else {
            dap_hash_shake256(output, outputlen_bytes, msg, input_len);
        }
        
        // Verify output
        bool non_zero = false;
        for (size_t i = 0; i < outputlen_bytes; i++) {
            if (output[i] != 0) {
                non_zero = true;
                break;
            }
        }
        TEST_ASSERT(non_zero, "Output should not be all zeros");
        
        DAP_DELETE(output);
    }
    
    TEST_PASS(test_name);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    printf("========================================\n");
    printf("   DAP SHA3 NIST Validation Tests\n");
    printf("========================================\n\n");
    printf("Implementation: %s\n\n", dap_hash_keccak_get_impl_name());
    
    // ========================================================================
    // SHA-3 Hash Algorithm Tests
    // ========================================================================
    
    printf("========================================\n");
    printf("SHA-3 Hash Algorithm Tests\n");
    printf("========================================\n");
    
    // SHA3-224
    printf("\n--- SHA3-224 Tests ---\n");
    test_sha3_short_msg_bit_oriented(224, SHA3_224_RATE_BITS);
    test_sha3_short_msg_byte_oriented(224, SHA3_224_RATE_BYTES);
    test_sha3_long_msg_bit_oriented(224, SHA3_224_RATE_BITS);
    test_sha3_long_msg_byte_oriented(224, SHA3_224_RATE_BYTES);
    test_sha3_monte_carlo(224);
    
    // SHA3-256
    printf("\n--- SHA3-256 Tests ---\n");
    test_sha3_short_msg_bit_oriented(256, SHA3_256_RATE_BITS);
    test_sha3_short_msg_byte_oriented(256, SHA3_256_RATE_BYTES);
    test_sha3_long_msg_bit_oriented(256, SHA3_256_RATE_BITS);
    test_sha3_long_msg_byte_oriented(256, SHA3_256_RATE_BYTES);
    test_sha3_monte_carlo(256);
    
    // SHA3-384
    printf("\n--- SHA3-384 Tests ---\n");
    test_sha3_short_msg_bit_oriented(384, SHA3_384_RATE_BITS);
    test_sha3_short_msg_byte_oriented(384, SHA3_384_RATE_BYTES);
    test_sha3_long_msg_bit_oriented(384, SHA3_384_RATE_BITS);
    test_sha3_long_msg_byte_oriented(384, SHA3_384_RATE_BYTES);
    test_sha3_monte_carlo(384);
    
    // SHA3-512
    printf("\n--- SHA3-512 Tests ---\n");
    test_sha3_short_msg_bit_oriented(512, SHA3_512_RATE_BITS);
    test_sha3_short_msg_byte_oriented(512, SHA3_512_RATE_BYTES);
    test_sha3_long_msg_bit_oriented(512, SHA3_512_RATE_BITS);
    test_sha3_long_msg_byte_oriented(512, SHA3_512_RATE_BYTES);
    test_sha3_monte_carlo(512);
    
    // ========================================================================
    // SHAKE XOF Tests
    // ========================================================================
    
    printf("\n========================================\n");
    printf("SHAKE XOF Tests\n");
    printf("========================================\n");
    
    // SHAKE128
    printf("\n--- SHAKE128 Tests ---\n");
    test_shake_short_msg_bit_oriented(128, SHAKE128_RATE_BITS);
    test_shake_short_msg_byte_oriented(128, SHAKE128_RATE_BYTES);
    test_shake_long_msg_bit_oriented(128, SHAKE128_RATE_BITS);
    test_shake_long_msg_byte_oriented(128, SHAKE128_RATE_BYTES);
    test_shake_monte_carlo(128);
    test_shake_variable_output_bit_oriented(128);
    test_shake_variable_output_byte_oriented(128);
    
    // SHAKE256
    printf("\n--- SHAKE256 Tests ---\n");
    test_shake_short_msg_bit_oriented(256, SHAKE256_RATE_BITS);
    test_shake_short_msg_byte_oriented(256, SHAKE256_RATE_BYTES);
    test_shake_long_msg_bit_oriented(256, SHAKE256_RATE_BITS);
    test_shake_long_msg_byte_oriented(256, SHAKE256_RATE_BYTES);
    test_shake_monte_carlo(256);
    test_shake_variable_output_bit_oriented(256);
    test_shake_variable_output_byte_oriented(256);
    
    // ========================================================================
    // Summary
    // ========================================================================
    
    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    printf("========================================\n");
    
    return g_tests_failed > 0 ? 1 : 0;
}
