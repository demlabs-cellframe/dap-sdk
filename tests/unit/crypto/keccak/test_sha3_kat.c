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
#include "dap_hash_shake128.h"
#include "dap_hash_shake256.h"

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
// SHA3-384 (FIPS 202) — exercises rate=104, output through sponge_squeeze
// ============================================================================

static void test_sha3_384_empty(void)
{
    /* FIPS 202 SHA3-384("") */
    const char *expected =
        "0c63a75b845e4f7d01107d852e4c2485c51a50aaaa94fc61995e71bbee983a2a"
        "c3713831264adb47fb6bd1e058d5f004";
    uint8_t out[DAP_HASH_SHA3_384_SIZE];
    dap_hash_sha3_384(out, (const uint8_t *)"", 0);
    TEST_ASSERT(compare_hash(out, expected, DAP_HASH_SHA3_384_SIZE) == 0,
                "SHA3-384(empty)");
    TEST_PASS("SHA3-384(empty)");
}

static void test_sha3_384_abc(void)
{
    /* FIPS 202 SHA3-384("abc") */
    const char *expected =
        "ec01498288516fc926459f58e2c6ad8df9b473cb0fc08c2596da7cf0e49be4b2"
        "98d88cea927ac7f539f1edf228376d25";
    uint8_t out[DAP_HASH_SHA3_384_SIZE];
    dap_hash_sha3_384(out, (const uint8_t *)"abc", 3);
    TEST_ASSERT(compare_hash(out, expected, DAP_HASH_SHA3_384_SIZE) == 0,
                "SHA3-384(abc)");
    TEST_PASS("SHA3-384(\"abc\")");
}

// ============================================================================
// SHAKE128 / SHAKE256 — FIPS 202 KAT vectors
// ============================================================================

static void test_shake128_empty_oneshot(void)
{
    /* FIPS 202 SHAKE128(empty), first 32 output bytes:
     *   7f 9c 2b a4 e8 8f 82 7d 61 60 45 50 76 05 85 3e
     *   d7 3b 80 93 f6 ef bc 88 eb 1a 6e ac fa 66 ef 26
     */
    const char *expected =
        "7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26";
    uint8_t out[32];
    dap_hash_shake128(out, sizeof(out), (const uint8_t *)"", 0);
    TEST_ASSERT(compare_hash(out, expected, 32) == 0,
                "SHAKE128(empty)[0..32]");
    TEST_PASS("SHAKE128(empty) one-shot 32B");
}

static void test_shake128_abc_long_oneshot(void)
{
    /* FIPS 202 SHAKE128("abc"), first 32 output bytes:
     *   58 81 09 2d d8 18 bf 5c f8 a3 dd b7 93 fb cb a7
     *   40 97 d5 c5 26 a6 d3 5f 97 b8 33 51 94 0f 2c c8
     */
    const char *expected =
        "5881092dd818bf5cf8a3ddb793fbcba74097d5c526a6d35f97b83351940f2cc8";
    uint8_t out[32];
    dap_hash_shake128(out, sizeof(out), (const uint8_t *)"abc", 3);
    TEST_ASSERT(compare_hash(out, expected, 32) == 0,
                "SHAKE128(abc)[0..32]");
    TEST_PASS("SHAKE128(\"abc\") one-shot 32B");
}

static void test_shake128_streaming_blocks(void)
{
    /* Verify that streaming squeezeblocks(N=1) repeatedly is equivalent to a
     * single squeezeblocks(N=k).  Both must also match the one-shot API.
     * This is the streaming-correctness property of the FIPS 202 sponge.
     */
    uint8_t st_seq[25 * 8] __attribute__((aligned(8)));
    uint8_t st_par[25 * 8] __attribute__((aligned(8)));
    uint64_t *l_seq = (uint64_t *)st_seq;
    uint64_t *l_par = (uint64_t *)st_par;
    enum { K = 4 };
    uint8_t out_seq[K * DAP_SHAKE128_RATE];
    uint8_t out_par[K * DAP_SHAKE128_RATE];

    dap_hash_shake128_absorb(l_seq, (const uint8_t *)"abc", 3);
    dap_hash_shake128_absorb(l_par, (const uint8_t *)"abc", 3);

    for (int i = 0; i < K; i++)
        dap_hash_shake128_squeezeblocks(out_seq + i * DAP_SHAKE128_RATE,
                                         1, l_seq);
    dap_hash_shake128_squeezeblocks(out_par, K, l_par);

    TEST_ASSERT(memcmp(out_seq, out_par, sizeof(out_seq)) == 0,
                "streaming N=1*K must equal N=K");

    /* Also matches one-shot. */
    uint8_t one[K * DAP_SHAKE128_RATE];
    dap_hash_shake128(one, sizeof(one), (const uint8_t *)"abc", 3);
    TEST_ASSERT(memcmp(one, out_par, sizeof(one)) == 0,
                "one-shot must equal streaming");
    TEST_PASS("SHAKE128 streaming = one-shot");
}

static void test_shake128_partial_tail(void)
{
    /* Output length not a multiple of the rate must use the partial-tail
     * branch of dap_hash_shake128 and still match a longer one-shot output
     * for the common prefix.
     */
    const size_t L = DAP_SHAKE128_RATE + 7;
    uint8_t out[L];
    uint8_t ref[L + DAP_SHAKE128_RATE];
    dap_hash_shake128(out, L,    (const uint8_t *)"abc", 3);
    dap_hash_shake128(ref, sizeof(ref), (const uint8_t *)"abc", 3);
    TEST_ASSERT(memcmp(out, ref, L) == 0,
                "SHAKE128 partial-tail consistency");
    TEST_PASS("SHAKE128 partial-tail length");
}

static void test_shake256_empty_oneshot(void)
{
    /* FIPS 202 SHAKE256(empty), first 32 output bytes:
     *   46 b9 dd 2b 0b a8 8d 13 23 3b 3f eb 74 3e eb 24
     *   3f cd 52 ea 62 b8 1b 82 b5 0c 27 64 6e d5 76 2f
     */
    const char *expected =
        "46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f";
    uint8_t out[32];
    dap_hash_shake256(out, sizeof(out), (const uint8_t *)"", 0);
    TEST_ASSERT(compare_hash(out, expected, 32) == 0,
                "SHAKE256(empty)[0..32]");
    TEST_PASS("SHAKE256(empty) one-shot 32B");
}

static void test_shake256_abc_oneshot(void)
{
    /* FIPS 202 SHAKE256("abc"), first 32 output bytes:
     *   48 33 66 60 13 60 a8 77 1c 68 63 08 0c c4 11 4d
     *   8d b4 45 30 f8 f1 e1 ee 4f 94 ea 37 e7 8b 57 39
     */
    const char *expected =
        "483366601360a8771c6863080cc4114d8db44530f8f1e1ee4f94ea37e78b5739";
    uint8_t out[32];
    dap_hash_shake256(out, sizeof(out), (const uint8_t *)"abc", 3);
    TEST_ASSERT(compare_hash(out, expected, 32) == 0,
                "SHAKE256(abc)[0..32]");
    TEST_PASS("SHAKE256(\"abc\") one-shot 32B");
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

    printf("\n--- SHA3-384 Core Tests (FIPS 202) ---\n");
    test_sha3_384_empty();
    test_sha3_384_abc();

    printf("\n--- SHAKE128 Core Tests (FIPS 202) ---\n");
    test_shake128_empty_oneshot();
    test_shake128_abc_long_oneshot();
    test_shake128_streaming_blocks();
    test_shake128_partial_tail();

    printf("\n--- SHAKE256 Core Tests (FIPS 202) ---\n");
    test_shake256_empty_oneshot();
    test_shake256_abc_oneshot();

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
