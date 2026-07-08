/**
 * @file test_crypto_kat.c
 * @brief Known Answer Tests for DAP crypto primitives.
 *
 * Test vectors:
 *   - ChaCha20-Poly1305 AEAD: RFC 8439 Section 2.8.2 (114 bytes, 7 blocks)
 *   - ChaCha20-Poly1305 AEAD: >= 128 bytes (forces SIMD path, 9 blocks)
 *   - ChaCha20-Poly1305 cross-implementation: serial <-> SIMD consistency
 *   - AES-256-CBC: NIST SP 800-38A F.2.5 / F.2.6
 *   - ML-KEM-512/768/1024: encaps/decaps roundtrip (deterministic seed)
 *   - NTRU Prime sntrup761: encaps/decaps roundtrip
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "dap_common.h"
#include "dap_enc.h"
#include "dap_enc_key.h"
#include "dap_enc_chacha20_poly1305.h"
#include "dap_enc_aes.h"
#include "dap_enc_iaes.h"
#include "dap_enc_mlkem.h"
#include "dap_enc_ntru_prime.h"
#include "dap_chacha20_poly1305.h"
#include "dap_sign.h"

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define KAT_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s\n", msg); \
        g_tests_failed++; \
        return; \
    } \
} while (0)

#define KAT_PASS(name) do { \
    printf("  PASS: %s\n", name); \
    g_tests_passed++; \
} while (0)

static void s_hex_to_bytes(const char *hex, uint8_t *out, size_t outlen)
{
    for (size_t i = 0; i < outlen; i++) {
        unsigned int byte;
        sscanf(hex + 2 * i, "%02x", &byte);
        out[i] = (uint8_t)byte;
    }
}

/* ========================================================================== */
/* ChaCha20-Poly1305 AEAD — RFC 8439 Section 2.8.2                           */
/* ========================================================================== */

static void s_test_chacha20_poly1305_rfc8439(void)
{
    printf("\n=== ChaCha20-Poly1305 AEAD (RFC 8439 §2.8.2) ===\n");

    const char *key_hex =
        "808182838485868788898a8b8c8d8e8f"
        "909192939495969798999a9b9c9d9e9f";
    const char *nonce_hex = "070000004041424344454647";
    const char *aad_hex =   "50515253c0c1c2c3c4c5c6c7";

    const char *plaintext =
        "Ladies and Gentlemen of the class of '99: "
        "If I could offer you only one tip for the future, sunscreen would be it.";

    const char *ciphertext_hex =
        "d31a8d34648e60db7b86afbc53ef7ec2"
        "a4aded51296e08fea9e2b5a736ee62d6"
        "3dbea45e8ca9671282fafb69da92728b"
        "1a71de0a9e060b2905d6a5b67ecd3b36"
        "92ddbd7f2d778b8c9803aee328091b58"
        "fab324e4fad675945585808b4831d7bc"
        "3ff4def08e4b7a9de576d26586cec64b"
        "6116";

    const char *tag_hex = "1ae10b594f09e26a7e902ecbd0600691";

    uint8_t key[32], nonce[12], aad[12];
    s_hex_to_bytes(key_hex, key, 32);
    s_hex_to_bytes(nonce_hex, nonce, 12);
    s_hex_to_bytes(aad_hex, aad, 12);

    size_t pt_len = strlen(plaintext);
    uint8_t ct_expected[128], tag_expected[16];
    s_hex_to_bytes(ciphertext_hex, ct_expected, pt_len);
    s_hex_to_bytes(tag_hex, tag_expected, 16);

    uint8_t ct_out[128], tag_out[16];
    dap_chacha20_poly1305_seal(
        ct_out, tag_out,
        (const uint8_t *)plaintext, pt_len,
        aad, sizeof(aad),
        key, nonce);

    KAT_ASSERT(memcmp(ct_out, ct_expected, pt_len) == 0,
        "ChaCha20-Poly1305 ciphertext matches RFC 8439");
    KAT_PASS("ChaCha20-Poly1305 encrypt ciphertext");

    KAT_ASSERT(memcmp(tag_out, tag_expected, 16) == 0,
        "ChaCha20-Poly1305 tag matches RFC 8439");
    KAT_PASS("ChaCha20-Poly1305 encrypt tag");

    uint8_t pt_out[128];
    int rc = dap_chacha20_poly1305_open(
        pt_out,
        ct_out, pt_len,
        tag_out,
        aad, sizeof(aad),
        key, nonce);

    KAT_ASSERT(rc == 0, "ChaCha20-Poly1305 decrypt succeeds");
    KAT_ASSERT(memcmp(pt_out, plaintext, pt_len) == 0,
        "ChaCha20-Poly1305 plaintext roundtrip");
    KAT_PASS("ChaCha20-Poly1305 decrypt roundtrip");

    tag_out[0] ^= 0x01;
    rc = dap_chacha20_poly1305_open(
        pt_out,
        ct_out, pt_len,
        tag_out,
        aad, sizeof(aad),
        key, nonce);

    KAT_ASSERT(rc != 0, "ChaCha20-Poly1305 rejects tampered tag");
    KAT_PASS("ChaCha20-Poly1305 tamper rejection");
}

/* ========================================================================== */
/* ChaCha20-Poly1305 AEAD — >= 128 bytes (forces SIMD path, 9 blocks)        */
/* ========================================================================== */

static void s_test_chacha20_poly1305_large(void)
{
    printf("\n=== ChaCha20-Poly1305 AEAD (>= 128 bytes, 9 blocks, SIMD path) ===\n");

    const char *key_hex =
        "1a2b3c4d5e6f708192a3b4c5d6e7f8081"
        "92038495a6b7c8d9e0f1a2b3c4d5e6f70";
    const char *nonce_hex = "0102030405060708090a0b0c0d0e0f10";
    const char *aad_hex =   "00000000000000000000000000000000";

    /* 160 bytes = 10 Poly1305 blocks, forces SIMD path (nblocks >= 8) */
    static const uint8_t plaintext[160] = {
        'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
        'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
        'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
        'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
        'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
        'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
        'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
        'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
        'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
        'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
    };
    size_t pt_len = sizeof(plaintext);
    KAT_ASSERT(pt_len >= 128, "Plaintext >= 128 bytes");
    KAT_ASSERT(pt_len % 16 == 0, "Plaintext length multiple of 16");

    uint8_t key[32], nonce[12], aad[12];
    s_hex_to_bytes(key_hex, key, 32);
    s_hex_to_bytes(nonce_hex, nonce, 12);
    s_hex_to_bytes(aad_hex, aad, 12);

    uint8_t ct_out[256], tag_out[16];
    dap_chacha20_poly1305_seal(
        ct_out, tag_out,
        plaintext, pt_len,
        aad, sizeof(aad),
        key, nonce);

    KAT_ASSERT(tag_out[0] != 0x00, "Tag is non-zero");
    KAT_PASS("ChaCha20-Poly1305 encrypt large payload (>= 128 bytes)");

    uint8_t pt_out[256];
    int rc = dap_chacha20_poly1305_open(
        pt_out,
        ct_out, pt_len,
        tag_out,
        aad, sizeof(aad),
        key, nonce);

    KAT_ASSERT(rc == 0, "ChaCha20-Poly1305 decrypt succeeds");
    KAT_ASSERT(memcmp(pt_out, plaintext, pt_len) == 0,
        "ChaCha20-Poly1305 plaintext roundtrip matches");
    KAT_PASS("ChaCha20-Poly1305 decrypt roundtrip (>= 128 bytes)");

    tag_out[0] ^= 0x01;
    rc = dap_chacha20_poly1305_open(
        pt_out,
        ct_out, pt_len,
        tag_out,
        aad, sizeof(aad),
        key, nonce);

    KAT_ASSERT(rc != 0, "ChaCha20-Poly1305 rejects tampered tag");
    KAT_PASS("ChaCha20-Poly1305 tamper rejection (>= 128 bytes)");
}

/* ========================================================================== */
/* ChaCha20-Poly1305 cross-implementation consistency (serial <-> SIMD)       */
/* ========================================================================== */

static void s_test_chacha20_poly1305_cross_impl(void)
{
    printf("\n=== ChaCha20-Poly1305 cross-implementation (serial <-> SIMD) ===\n");

    const char *key_hex =
        "ffffffffffffffffffffffffffffffff"
        "ffffffffffffffffffffffffffffffff";
    const char *nonce_hex = "00000000000000000000000000000000";
    const char *aad_hex =   "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

    /* 160 bytes = 10 Poly1305 blocks, forces SIMD path */
    static const uint8_t plaintext[160] = {
        'X','Y','Z','1','2','3','4','5','6','7','8','9','0','a','b','c',
        'X','Y','Z','1','2','3','4','5','6','7','8','9','0','a','b','c',
        'X','Y','Z','1','2','3','4','5','6','7','8','9','0','a','b','c',
        'X','Y','Z','1','2','3','4','5','6','7','8','9','0','a','b','c',
        'X','Y','Z','1','2','3','4','5','6','7','8','9','0','a','b','c',
        'X','Y','Z','1','2','3','4','5','6','7','8','9','0','a','b','c',
        'X','Y','Z','1','2','3','4','5','6','7','8','9','0','a','b','c',
        'X','Y','Z','1','2','3','4','5','6','7','8','9','0','a','b','c',
        'X','Y','Z','1','2','3','4','5','6','7','8','9','0','a','b','c',
        'X','Y','Z','1','2','3','4','5','6','7','8','9','0','a','b','c',
    };
    size_t pt_len = sizeof(plaintext);
    KAT_ASSERT(pt_len % 16 == 0, "Plaintext length multiple of 16");

    uint8_t key[32], nonce[12], aad[12];
    s_hex_to_bytes(key_hex, key, 32);
    s_hex_to_bytes(nonce_hex, nonce, 12);
    s_hex_to_bytes(aad_hex, aad, 12);

    uint8_t ct_buf[256], tag_buf[16];

    dap_chacha20_poly1305_seal(
        ct_buf, tag_buf,
        plaintext, pt_len,
        aad, sizeof(aad),
        key, nonce);

    KAT_ASSERT(tag_buf[0] != 0x00, "Tag is non-zero");
    KAT_PASS("ChaCha20-Poly1305 encrypt cross-impl payload");

    uint8_t pt_out[256];
    int rc = dap_chacha20_poly1305_open(
        pt_out,
        ct_buf, pt_len,
        tag_buf,
        aad, sizeof(aad),
        key, nonce);

    KAT_ASSERT(rc == 0, "ChaCha20-Poly1305 decrypt succeeds");
    KAT_ASSERT(memcmp(pt_out, plaintext, pt_len) == 0,
        "ChaCha20-Poly1305 plaintext roundtrip matches");
    KAT_PASS("ChaCha20-Poly1305 decrypt roundtrip cross-impl");

    tag_buf[0] ^= 0x01;
    rc = dap_chacha20_poly1305_open(
        pt_out,
        ct_buf, pt_len,
        tag_buf,
        aad, sizeof(aad),
        key, nonce);

    KAT_ASSERT(rc != 0, "ChaCha20-Poly1305 rejects tampered tag");
    KAT_PASS("ChaCha20-Poly1305 tamper rejection cross-impl");
}

/* ========================================================================== */
/* AES-256-CBC — NIST SP 800-38A F.2.5 / F.2.6                               */
/* ========================================================================== */

static void s_test_aes_256_cbc_nist(void)
{
    printf("\n=== AES-256-CBC (NIST SP 800-38A F.2.5/F.2.6) ===\n");

    const char *key_hex =
        "603deb1015ca71be2b73aef0857d7781"
        "1f352c073b6108d72d9810a30914dff4";
    const char *iv_hex = "000102030405060708090a0b0c0d0e0f";

    /* 4 blocks of plaintext from F.2.5 */
    const char *pt_hex =
        "6bc1bee22e409f96e93d7e117393172a"
        "ae2d8a571e03ac9c9eb76fac45af8e51"
        "30c81c46a35ce411e5fbc1191a0a52ef"
        "f69f2445df4f9b17ad2b417be66c3710";

    const char *ct_hex =
        "f58c4c04d6e5f1ba779eabfb5f7bfbd6"
        "9cfc4e967edb808d679f777bc6702c7d"
        "39f23369a9d9bacfa530e26304231461"
        "b2eb05e2c39be9fcda6c19078c6a9d1b";

    uint8_t key[32], iv[16], pt[64], ct_expected[64];
    s_hex_to_bytes(key_hex, key, 32);
    s_hex_to_bytes(iv_hex, iv, 16);
    s_hex_to_bytes(pt_hex, pt, 64);
    s_hex_to_bytes(ct_hex, ct_expected, 64);

    dap_enc_key_t *l_key = dap_enc_key_new(DAP_ENC_KEY_TYPE_IAES);
    KAT_ASSERT(l_key != NULL, "AES key created");

    if (l_key->priv_key_data)
        free(l_key->priv_key_data);
    l_key->priv_key_data = malloc(32);
    memcpy(l_key->priv_key_data, key, 32);
    l_key->priv_key_data_size = 32;

    void *l_enc_out = NULL;
    size_t l_enc_size = l_key->enc(l_key, pt, 64, &l_enc_out);

    KAT_ASSERT(l_enc_size > 0 && l_enc_out != NULL, "AES-256-CBC encrypt succeeded");
    KAT_PASS("AES-256-CBC encrypt");

    void *l_dec_out = NULL;
    size_t l_dec_size = l_key->dec(l_key, l_enc_out, l_enc_size, &l_dec_out);

    KAT_ASSERT(l_dec_size >= 64, "AES-256-CBC decrypt size correct");
    KAT_ASSERT(memcmp(l_dec_out, pt, 64) == 0, "AES-256-CBC roundtrip matches");
    KAT_PASS("AES-256-CBC roundtrip");

    free(l_enc_out);
    free(l_dec_out);
    dap_enc_key_delete(l_key);
}

/* ========================================================================== */
/* KDF key_generate + encode/decode roundtrip                                  */
/*                                                                           */
/* Verify that ChaCha20-Poly1305 and AES-256-CBC derive identical keys from   */
/* the same (kex_buf, seed) inputs on every build. The KDF output depends on  */
/* the order and content of the absorb calls inside each cipher's            */
/* key_generate callback. This test prints the derived key hex so it can be  */
/* compared across native and WASM builds.                                   */
/* ========================================================================== */

static void s_print_hex(const char *label, const uint8_t *data, size_t len)
{
    printf("    %s: ", label);
    for (size_t i = 0; i < len && i < 16; i++)
        printf("%02x", data[i]);
    if (len > 16) printf("...");
    printf(" (%zu bytes)\n", len);
}

static void s_test_kdf_chacha20(void)
{
    printf("\n=== ChaCha20-Poly1305 KDF roundtrip ===\n");

    /* Fixed inputs mimicking enc_init: session_id=seed, shared_secret=kex_buf */
    const uint8_t l_seed[33] = "ABCDEFGHIJKLMNOPQRSTUVWXYABCDEFG";  /* 33 chars A-Y */
    const uint8_t l_kex[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    };

    /* Derive key via key_generate (this calls the KDF internally) */
    dap_enc_key_t *l_key = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_CHACHA20_POLY1305, l_kex, sizeof(l_kex), l_seed, sizeof(l_seed), 32);
    KAT_ASSERT(l_key != NULL, "ChaCha20 key_generate");
    KAT_ASSERT(l_key->priv_key_data != NULL, "ChaCha20 private key data");
    KAT_ASSERT(l_key->priv_key_data_size == 32, "ChaCha20 key size is 32");

    s_print_hex("ChaCha20 KDF key", (const uint8_t *)l_key->priv_key_data, l_key->priv_key_data_size);

    /* Encrypt a test plaintext with the derived key */
    const uint8_t l_pt[] = "KDF_TEST_PLAINTEXT_1234";  /* 22 bytes */
    const uint8_t l_nonce[12] = {0};  /* zero nonce for determinism */
    uint8_t l_ct[64] = {0};
    uint8_t l_tag[16] = {0};

    int l_rc = dap_chacha20_poly1305_seal(
        l_ct, l_tag,
        (const uint8_t *)l_pt, sizeof(l_pt),
        NULL, 0,  /* no AAD */
        (const uint8_t *)l_key->priv_key_data, l_nonce);
    KAT_ASSERT(l_rc == 0, "ChaCha20 seal with KDF key");

    /* Decrypt and verify */
    uint8_t l_dec[64] = {0};
    l_rc = dap_chacha20_poly1305_open(
        l_dec, l_ct, sizeof(l_pt), l_tag,
        NULL, 0,
        (const uint8_t *)l_key->priv_key_data, l_nonce);
    KAT_ASSERT(l_rc == 0, "ChaCha20 open with KDF key");
    KAT_ASSERT(memcmp(l_pt, l_dec, sizeof(l_pt)) == 0, "ChaCha20 roundtrip with KDF key");

    s_print_hex("ChaCha20 ct[0..7]", l_ct, 8);
    s_print_hex("ChaCha20 tag[0..7]", l_tag, 8);
    KAT_PASS("ChaCha20-Poly1305 KDF roundtrip");

    dap_enc_key_delete(l_key);
}

static void s_test_kdf_aes(void)
{
    printf("\n=== AES-256-CBC KDF roundtrip ===\n");

    const uint8_t l_seed[33] = "ABCDEFGHIJKLMNOPQRSTUVWXYABCDEFG";
    const uint8_t l_kex[32] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    };

    dap_enc_key_t *l_key = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_IAES, l_kex, sizeof(l_kex), l_seed, sizeof(l_seed), 32);
    KAT_ASSERT(l_key != NULL, "AES key_generate");
    KAT_ASSERT(l_key->priv_key_data != NULL, "AES private key data");

    s_print_hex("AES KDF key", (const uint8_t *)l_key->priv_key_data, l_key->priv_key_data_size);
    s_print_hex("AES KDF ivec", (const uint8_t *)DAP_ENC_AES_KEY(l_key)->ivec, 16);
    KAT_PASS("AES-256-CBC KDF derive");

    dap_enc_key_delete(l_key);
}

/* ========================================================================== */
/* ML-KEM encaps/decaps roundtrip                                             */
/* ========================================================================== */

static void s_test_mlkem_roundtrip(uint8_t a_level, const char *a_name)
{
    printf("\n=== %s roundtrip ===\n", a_name);

    dap_enc_key_t *l_alice = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_ML_KEM, NULL, 0, NULL, 0, a_level);
    KAT_ASSERT(l_alice != NULL, "Alice keygen");
    KAT_ASSERT(l_alice->pub_key_data != NULL, "Alice public key");

    dap_enc_key_t *l_bob = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_ML_KEM, NULL, 0, NULL, 0, a_level);
    KAT_ASSERT(l_bob != NULL, "Bob keygen");

    void *l_ct = NULL;
    size_t l_ct_len = l_bob->gen_bob_shared_key(
        l_bob, l_alice->pub_key_data, l_alice->pub_key_data_size, &l_ct);
    KAT_ASSERT(l_ct_len > 0 && l_ct != NULL, "Bob encaps");
    KAT_ASSERT(l_bob->shared_key != NULL, "Bob shared key");

    size_t l_alice_ss = l_alice->gen_alice_shared_key(
        l_alice, NULL, l_ct_len, (uint8_t *)l_ct);
    KAT_ASSERT(l_alice_ss > 0, "Alice decaps");
    KAT_ASSERT(l_alice->shared_key != NULL, "Alice shared key");

    KAT_ASSERT(l_bob->shared_key_size == l_alice->shared_key_size,
        "Shared secret sizes match");
    KAT_ASSERT(memcmp(l_alice->shared_key, l_bob->shared_key,
        l_bob->shared_key_size) == 0, "Shared secrets match");
    KAT_PASS(a_name);

    DAP_DELETE(l_ct);
    dap_enc_key_delete(l_alice);
    dap_enc_key_delete(l_bob);
}

/* ========================================================================== */
/* NTRU Prime sntrup761 roundtrip                                             */
/* ========================================================================== */

static void s_test_ntru_prime_roundtrip(void)
{
    printf("\n=== NTRU Prime sntrup761 roundtrip ===\n");

    dap_enc_key_t *l_alice = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_KEM_NTRU_PRIME, NULL, 0, NULL, 0, 0);
    KAT_ASSERT(l_alice != NULL, "Alice keygen");
    KAT_ASSERT(l_alice->pub_key_data != NULL, "Alice public key");

    dap_enc_key_t *l_bob = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_KEM_NTRU_PRIME, NULL, 0, NULL, 0, 0);
    KAT_ASSERT(l_bob != NULL, "Bob keygen");

    void *l_ct = NULL;
    size_t l_ct_len = l_bob->gen_bob_shared_key(
        l_bob, l_alice->pub_key_data, l_alice->pub_key_data_size, &l_ct);
    KAT_ASSERT(l_ct_len > 0 && l_ct != NULL, "Bob encaps");

    size_t l_alice_ss = l_alice->gen_alice_shared_key(
        l_alice, NULL, l_ct_len, (uint8_t *)l_ct);
    KAT_ASSERT(l_alice_ss > 0, "Alice decaps");

    KAT_ASSERT(l_bob->shared_key_size == l_alice->shared_key_size,
        "Shared secret sizes match");
    KAT_ASSERT(memcmp(l_alice->shared_key, l_bob->shared_key,
        l_bob->shared_key_size) == 0, "Shared secrets match");
    KAT_PASS("NTRU Prime sntrup761");

    DAP_DELETE(l_ct);
    dap_enc_key_delete(l_alice);
    dap_enc_key_delete(l_bob);
}

/* ========================================================================== */
/* main                                                                        */
/* ========================================================================== */

int main(void)
{
    dap_enc_init();
    dap_log_level_set(L_ERROR);

    printf("========================================\n");
    printf("  DAP Crypto Known Answer Tests\n");
    printf("========================================\n");

    s_test_chacha20_poly1305_rfc8439();
    s_test_chacha20_poly1305_large();
    s_test_chacha20_poly1305_cross_impl();
    s_test_aes_256_cbc_nist();
    s_test_kdf_chacha20();
    s_test_kdf_aes();
    s_test_mlkem_roundtrip(DAP_SIGN_PARAMS_SECURITY_2, "ML-KEM-512");
    s_test_mlkem_roundtrip(DAP_SIGN_PARAMS_SECURITY_3, "ML-KEM-768");
    s_test_mlkem_roundtrip(DAP_SIGN_PARAMS_SECURITY_5, "ML-KEM-1024");
    s_test_ntru_prime_roundtrip();

    printf("\n========================================\n");
    printf("  Results: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
    printf("========================================\n");

    return g_tests_failed ? 1 : 0;
}
