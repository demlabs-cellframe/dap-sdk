/**
 * @file test_sha3_kat.c
 * @brief SHA3 Known Answer Tests (KAT) using NIST CAVP test vectors
 * @details Verifies correctness of DAP SHA3 implementation against official vectors
 *
 * @author DAP SDK Team
 * @copyright DeM Labs Inc. 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "dap_common.h"
#include "dap_hash_sha3.h"
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

static int compare_hash(const uint8_t *computed, const char *expected_hex, size_t len)
{
    uint8_t expected[64];
    hex_to_bytes(expected_hex, expected, len);
    return memcmp(computed, expected, len);
}

// ============================================================================
// SHA3-256 Test Vectors (NIST CAVP)
// ============================================================================

static void test_sha3_256_empty(void)
{
    const char *expected = "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a";
    dap_hash_sha3_256_t hash;
    
    dap_hash_sha3_256("", 0, &hash);
    TEST_ASSERT(compare_hash(hash.raw, expected, 32) == 0, "SHA3-256 empty string");
    TEST_PASS("SHA3-256(\"\")");
}

static void test_sha3_256_abc(void)
{
    const char *expected = "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532";
    dap_hash_sha3_256_t hash;
    
    dap_hash_sha3_256("abc", 3, &hash);
    TEST_ASSERT(compare_hash(hash.raw, expected, 32) == 0, "SHA3-256('abc')");
    TEST_PASS("SHA3-256(\"abc\")");
}

static void test_sha3_256_448bit(void)
{
    const char *input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    const char *expected = "41c0dba2a9d6240849100376a8235e2c82e1b9998a999e21db32dd97496d3376";
    dap_hash_sha3_256_t hash;
    
    dap_hash_sha3_256(input, strlen(input), &hash);
    TEST_ASSERT(compare_hash(hash.raw, expected, 32) == 0, "SHA3-256(448-bit)");
    TEST_PASS("SHA3-256(448-bit message)");
}

static void test_sha3_256_896bit(void)
{
    const char *input = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";
    const char *expected = "916f6061fe879741ca6469b43971dfdb28b1a32dc36cb3254e812be27aad1d18";
    dap_hash_sha3_256_t hash;
    
    dap_hash_sha3_256(input, strlen(input), &hash);
    TEST_ASSERT(compare_hash(hash.raw, expected, 32) == 0, "SHA3-256(896-bit)");
    TEST_PASS("SHA3-256(896-bit message)");
}

// ============================================================================
// Utility function tests
// ============================================================================

static void test_sha3_256_to_str(void)
{
    dap_hash_sha3_256_t hash;
    dap_hash_sha3_256("abc", 3, &hash);
    
    char str[DAP_HASH_SHA3_256_STR_SIZE];
    int ret = dap_hash_sha3_256_to_str(&hash, str, DAP_HASH_SHA3_256_STR_SIZE);
    TEST_ASSERT(ret > 0, "dap_hash_sha3_256_to_str returns positive");
    TEST_ASSERT(strncmp(str, "0x", 2) == 0, "String starts with 0x");
    TEST_PASS("SHA3-256 to string conversion");
}

static void test_sha3_256_from_str(void)
{
    const char *hex_str = "0x3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532";
    dap_hash_sha3_256_t hash;
    
    int ret = dap_hash_sha3_256_from_hex_str(hex_str, &hash);
    TEST_ASSERT(ret == 0, "dap_hash_sha3_256_from_hex_str returns 0");
    
    // Verify by computing hash of "abc"
    dap_hash_sha3_256_t expected;
    dap_hash_sha3_256("abc", 3, &expected);
    TEST_ASSERT(dap_hash_sha3_256_compare(&hash, &expected), "Parsed hash matches computed");
    TEST_PASS("SHA3-256 from string parsing");
}

static void test_sha3_256_compare(void)
{
    dap_hash_sha3_256_t hash1, hash2;
    dap_hash_sha3_256("test1", 5, &hash1);
    dap_hash_sha3_256("test2", 5, &hash2);
    
    TEST_ASSERT(dap_hash_sha3_256_compare(&hash1, &hash1), "Same hash compares equal");
    TEST_ASSERT(!dap_hash_sha3_256_compare(&hash1, &hash2), "Different hashes compare not equal");
    TEST_PASS("SHA3-256 comparison");
}

static void test_sha3_256_is_blank(void)
{
    dap_hash_sha3_256_t blank = {};
    dap_hash_sha3_256_t non_blank;
    dap_hash_sha3_256("test", 4, &non_blank);
    
    TEST_ASSERT(dap_hash_sha3_256_is_blank(&blank), "Blank hash is blank");
    TEST_ASSERT(!dap_hash_sha3_256_is_blank(&non_blank), "Non-blank hash is not blank");
    TEST_PASS("SHA3-256 is_blank check");
}

// ============================================================================
// Edge Cases
// ============================================================================

static void test_large_message(void)
{
    size_t size = 1024 * 1024;
    uint8_t *data = DAP_NEW_Z_SIZE(uint8_t, size);
    dap_hash_sha3_256_t hash;
    
    if (!data) {
        printf("  SKIP: Could not allocate 1MB for test\n");
        return;
    }
    
    bool ret = dap_hash_sha3_256(data, size, &hash);
    TEST_ASSERT(ret, "SHA3-256 of 1MB succeeds");
    
    // Verify against known value (1MB of zeros) - computed with Python hashlib.sha3_256
    const char *expected = "7e1839fd5b1f59802cdf1f098dd5198e49b2a242ec43a5e2f107d2e2e57b0f25";
    TEST_ASSERT(compare_hash(hash.raw, expected, 32) == 0, "SHA3-256(1MB zeros)");
    TEST_PASS("SHA3-256(1MB zeros)");
    
    DAP_DELETE(data);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    printf("========================================\n");
    printf("   DAP SHA3 Known Answer Tests\n");
    printf("========================================\n\n");
    printf("Implementation: %s\n\n", dap_hash_keccak_get_impl_name());
    
    printf("--- SHA3-256 Core Tests ---\n");
    test_sha3_256_empty();
    test_sha3_256_abc();
    test_sha3_256_448bit();
    test_sha3_256_896bit();
    
    printf("\n--- SHA3-256 Utility Tests ---\n");
    test_sha3_256_to_str();
    test_sha3_256_from_str();
    test_sha3_256_compare();
    test_sha3_256_is_blank();
    
    printf("\n--- Edge Cases ---\n");
    test_large_message();
    
    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    printf("========================================\n");
    
    return g_tests_failed > 0 ? 1 : 0;
}
