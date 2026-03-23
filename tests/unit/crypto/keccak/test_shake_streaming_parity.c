#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dap_hash_shake128.h"
#include "dap_hash_shake256.h"

static int g_passed = 0;
static int g_failed = 0;

#define TEST_CHECK(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
        g_failed++; \
        return; \
    } \
} while (0)

#define TEST_PASS(msg) do { \
    printf("  PASS: %s\n", msg); \
    g_passed++; \
} while (0)

static void s_hex_to_bytes(const char *a_hex, uint8_t *a_out, size_t a_out_len)
{
    for (size_t i = 0; i < a_out_len; i++) {
        unsigned l_byte = 0;
        (void)sscanf(a_hex + 2 * i, "%02x", &l_byte);
        a_out[i] = (uint8_t)l_byte;
    }
}

static void s_shake256_stream(uint8_t *a_output, size_t a_outlen, const uint8_t *a_input, size_t a_inlen)
{
    uint64_t l_state[25];
    size_t l_nblocks = a_outlen / DAP_SHAKE256_RATE;

    dap_hash_shake256_absorb(l_state, a_input, a_inlen);
    dap_hash_shake256_squeezeblocks(a_output, l_nblocks, l_state);

    a_output += l_nblocks * DAP_SHAKE256_RATE;
    a_outlen -= l_nblocks * DAP_SHAKE256_RATE;

    if (a_outlen > 0) {
        uint8_t l_tmp[DAP_SHAKE256_RATE];
        dap_hash_shake256_squeezeblocks(l_tmp, 1, l_state);
        memcpy(a_output, l_tmp, a_outlen);
    }
}

static void s_shake128_stream(uint8_t *a_output, size_t a_outlen, const uint8_t *a_input, size_t a_inlen)
{
    uint64_t l_state[25];
    size_t l_nblocks = a_outlen / DAP_SHAKE128_RATE;

    dap_hash_shake128_absorb(l_state, a_input, a_inlen);
    dap_hash_shake128_squeezeblocks(a_output, l_nblocks, l_state);

    a_output += l_nblocks * DAP_SHAKE128_RATE;
    a_outlen -= l_nblocks * DAP_SHAKE128_RATE;

    if (a_outlen > 0) {
        uint8_t l_tmp[DAP_SHAKE128_RATE];
        dap_hash_shake128_squeezeblocks(l_tmp, 1, l_state);
        memcpy(a_output, l_tmp, a_outlen);
    }
}

static void s_cshake256_stream(uint8_t *a_output, size_t a_outlen, uint16_t a_cstm,
                               const uint8_t *a_input, size_t a_inlen)
{
    uint64_t l_state[25];
    size_t l_nblocks = a_outlen / DAP_SHAKE256_RATE;

    dap_hash_cshake256_simple_absorb(l_state, a_cstm, a_input, a_inlen);
    dap_hash_cshake256_simple_squeezeblocks(a_output, l_nblocks, l_state);

    a_output += l_nblocks * DAP_SHAKE256_RATE;
    a_outlen -= l_nblocks * DAP_SHAKE256_RATE;

    if (a_outlen > 0) {
        uint8_t l_tmp[DAP_SHAKE256_RATE];
        dap_hash_cshake256_simple_squeezeblocks(l_tmp, 1, l_state);
        memcpy(a_output, l_tmp, a_outlen);
    }
}

static void s_cshake128_stream(uint8_t *a_output, size_t a_outlen, uint16_t a_cstm,
                               const uint8_t *a_input, size_t a_inlen)
{
    uint64_t l_state[25];
    size_t l_nblocks = a_outlen / DAP_SHAKE128_RATE;

    dap_hash_cshake128_simple_absorb(l_state, a_cstm, a_input, a_inlen);
    dap_hash_cshake128_simple_squeezeblocks(a_output, l_nblocks, l_state);

    a_output += l_nblocks * DAP_SHAKE128_RATE;
    a_outlen -= l_nblocks * DAP_SHAKE128_RATE;

    if (a_outlen > 0) {
        uint8_t l_tmp[DAP_SHAKE128_RATE];
        dap_hash_cshake128_simple_squeezeblocks(l_tmp, 1, l_state);
        memcpy(a_output, l_tmp, a_outlen);
    }
}

static void s_test_shake256_kat_empty_64bytes(void)
{
    static const char *s_expected_hex =
        "46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f"
        "d75dc4ddd8c0f200cb05019d67b592f6fc821c49479ab48640292eacb3b7c4be";
    uint8_t l_expected[64];
    uint8_t l_out[64];

    s_hex_to_bytes(s_expected_hex, l_expected, sizeof(l_expected));
    dap_hash_shake256(l_out, sizeof(l_out), (const uint8_t *)"", 0);

    TEST_CHECK(memcmp(l_out, l_expected, sizeof(l_out)) == 0, "SHAKE256 KAT (empty, 64 bytes)");
    TEST_PASS("SHAKE256 KAT (empty, 64 bytes)");
}

static void s_test_shake128_kat_empty_64bytes(void)
{
    static const char *s_expected_hex =
        "7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26"
        "3cb1eea988004b93103cfb0aeefd2a686e01fa4a58e8a3639ca8a1e3f9ae57e2";
    uint8_t l_expected[64];
    uint8_t l_out[64];

    s_hex_to_bytes(s_expected_hex, l_expected, sizeof(l_expected));
    dap_hash_shake128(l_out, sizeof(l_out), (const uint8_t *)"", 0);

    TEST_CHECK(memcmp(l_out, l_expected, sizeof(l_out)) == 0, "SHAKE128 KAT (empty, 64 bytes)");
    TEST_PASS("SHAKE128 KAT (empty, 64 bytes)");
}

static void s_test_cshake256_kat_custom_42_64bytes(void)
{
    static const char *s_expected_hex =
        "9e484d6d808a194e0f82ac6b89ec75fcb98344d25c2ddc461fc950fbaffb4812"
        "0f1e581a2ee1369df2333cdd03d5d844e619886f1e774d5ff0b23eb0920b5f47";
    static const uint8_t s_input[] = "integration-cshake256-flow";
    uint8_t l_expected[64];
    uint8_t l_out[64];

    s_hex_to_bytes(s_expected_hex, l_expected, sizeof(l_expected));
    dap_hash_cshake256_simple(l_out, sizeof(l_out), 42, s_input, sizeof(s_input) - 1);

    TEST_CHECK(memcmp(l_out, l_expected, sizeof(l_out)) == 0, "cSHAKE256 KAT (custom=42, 64 bytes)");
    TEST_PASS("cSHAKE256 KAT (custom=42, 64 bytes)");
}

static void s_test_cshake128_kat_custom_42_64bytes(void)
{
    static const char *s_expected_hex =
        "38a6331c4dbe92b50e786096350d7088ef28671882df5e8e6cebe602a514f05f"
        "a77dd527b9786a16d4cbfd18f5520bfa7a02a9106060abbf411bfaae2c1554ba";
    static const uint8_t s_input[] = "integration-cshake128-flow";
    uint8_t l_expected[64];
    uint8_t l_out[64];

    s_hex_to_bytes(s_expected_hex, l_expected, sizeof(l_expected));
    dap_hash_cshake128_simple(l_out, sizeof(l_out), 42, s_input, sizeof(s_input) - 1);

    TEST_CHECK(memcmp(l_out, l_expected, sizeof(l_out)) == 0, "cSHAKE128 KAT (custom=42, 64 bytes)");
    TEST_PASS("cSHAKE128 KAT (custom=42, 64 bytes)");
}

static void s_test_shake256_parity(void)
{
    static const size_t s_outlens[] = {0, 1, 16, 32, 64, 136, 137, 200, 272};
    static const uint8_t s_input_empty[] = "";
    static const uint8_t s_input_short[] = "integration-shake256-flow";
    uint8_t l_input_136[136];
    uint8_t l_input_200[200];
    uint8_t l_one_shot[272];
    uint8_t l_stream[272];

    for (size_t i = 0; i < sizeof(l_input_136); i++) {
        l_input_136[i] = (uint8_t)i;
    }
    for (size_t i = 0; i < sizeof(l_input_200); i++) {
        l_input_200[i] = (uint8_t)(255u - (unsigned)i);
    }

    const struct {
        const char *name;
        const uint8_t *data;
        size_t len;
    } l_vectors[] = {
        {"empty", s_input_empty, 0},
        {"short", s_input_short, sizeof(s_input_short) - 1},
        {"136-bytes", l_input_136, sizeof(l_input_136)},
        {"200-bytes", l_input_200, sizeof(l_input_200)}
    };

    for (size_t v = 0; v < sizeof(l_vectors) / sizeof(l_vectors[0]); v++) {
        for (size_t i = 0; i < sizeof(s_outlens) / sizeof(s_outlens[0]); i++) {
            size_t l_outlen = s_outlens[i];
            dap_hash_shake256(l_one_shot, l_outlen, l_vectors[v].data, l_vectors[v].len);
            s_shake256_stream(l_stream, l_outlen, l_vectors[v].data, l_vectors[v].len);
            TEST_CHECK(memcmp(l_one_shot, l_stream, l_outlen) == 0, "SHAKE256 one-shot/stream parity");
        }
    }

    TEST_PASS("SHAKE256 one-shot/stream parity");
}

static void s_test_shake128_parity(void)
{
    static const size_t s_outlens[] = {0, 1, 16, 32, 64, 168, 169, 224, 336};
    static const uint8_t s_input_empty[] = "";
    static const uint8_t s_input_short[] = "integration-shake128-flow";
    uint8_t l_input_168[168];
    uint8_t l_input_256[256];
    uint8_t l_one_shot[336];
    uint8_t l_stream[336];

    for (size_t i = 0; i < sizeof(l_input_168); i++) {
        l_input_168[i] = (uint8_t)i;
    }
    for (size_t i = 0; i < sizeof(l_input_256); i++) {
        l_input_256[i] = (uint8_t)(255u - (unsigned)i);
    }

    const struct {
        const char *name;
        const uint8_t *data;
        size_t len;
    } l_vectors[] = {
        {"empty", s_input_empty, 0},
        {"short", s_input_short, sizeof(s_input_short) - 1},
        {"168-bytes", l_input_168, sizeof(l_input_168)},
        {"256-bytes", l_input_256, sizeof(l_input_256)}
    };

    for (size_t v = 0; v < sizeof(l_vectors) / sizeof(l_vectors[0]); v++) {
        for (size_t i = 0; i < sizeof(s_outlens) / sizeof(s_outlens[0]); i++) {
            size_t l_outlen = s_outlens[i];
            dap_hash_shake128(l_one_shot, l_outlen, l_vectors[v].data, l_vectors[v].len);
            s_shake128_stream(l_stream, l_outlen, l_vectors[v].data, l_vectors[v].len);
            TEST_CHECK(memcmp(l_one_shot, l_stream, l_outlen) == 0, "SHAKE128 one-shot/stream parity");
        }
    }

    TEST_PASS("SHAKE128 one-shot/stream parity");
}

static void s_test_cshake256_parity(void)
{
    static const size_t s_outlens[] = {0, 1, 16, 32, 64, 136, 137, 272};
    static const uint16_t s_customs[] = {0, 1, 42, 65535};
    static const uint8_t s_input[] = "integration-cshake256-flow";
    uint8_t l_one_shot[272];
    uint8_t l_stream[272];

    for (size_t c = 0; c < sizeof(s_customs) / sizeof(s_customs[0]); c++) {
        for (size_t i = 0; i < sizeof(s_outlens) / sizeof(s_outlens[0]); i++) {
            size_t l_outlen = s_outlens[i];
            dap_hash_cshake256_simple(l_one_shot, l_outlen, s_customs[c], s_input, sizeof(s_input) - 1);
            s_cshake256_stream(l_stream, l_outlen, s_customs[c], s_input, sizeof(s_input) - 1);
            TEST_CHECK(memcmp(l_one_shot, l_stream, l_outlen) == 0, "cSHAKE256 one-shot/stream parity");
        }
    }

    TEST_PASS("cSHAKE256 one-shot/stream parity");
}

static void s_test_cshake128_parity(void)
{
    static const size_t s_outlens[] = {0, 1, 16, 32, 64, 168, 169, 336};
    static const uint16_t s_customs[] = {0, 1, 42, 65535};
    static const uint8_t s_input[] = "integration-cshake128-flow";
    uint8_t l_one_shot[336];
    uint8_t l_stream[336];

    for (size_t c = 0; c < sizeof(s_customs) / sizeof(s_customs[0]); c++) {
        for (size_t i = 0; i < sizeof(s_outlens) / sizeof(s_outlens[0]); i++) {
            size_t l_outlen = s_outlens[i];
            dap_hash_cshake128_simple(l_one_shot, l_outlen, s_customs[c], s_input, sizeof(s_input) - 1);
            s_cshake128_stream(l_stream, l_outlen, s_customs[c], s_input, sizeof(s_input) - 1);
            TEST_CHECK(memcmp(l_one_shot, l_stream, l_outlen) == 0, "cSHAKE128 one-shot/stream parity");
        }
    }

    TEST_PASS("cSHAKE128 one-shot/stream parity");
}

int main(void)
{
    printf("========================================\n");
    printf("   SHAKE Streaming Parity Tests\n");
    printf("========================================\n\n");

    s_test_shake256_kat_empty_64bytes();
    s_test_shake128_kat_empty_64bytes();
    s_test_cshake256_kat_custom_42_64bytes();
    s_test_cshake128_kat_custom_42_64bytes();
    s_test_shake256_parity();
    s_test_shake128_parity();
    s_test_cshake256_parity();
    s_test_cshake128_parity();

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", g_passed, g_failed);
    printf("========================================\n");

    return g_failed == 0 ? 0 : 1;
}
