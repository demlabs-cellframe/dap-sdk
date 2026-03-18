/**
 * @file test_pqc_algorithms.c
 * @brief Unit tests for post-quantum and new symmetric algorithms:
 *        ML-DSA (FIPS 204), ML-KEM (FIPS 203), ChaCha20-Poly1305,
 *        NTRU Prime (sntrup761), unified AES-256-CBC.
 *
 * @authors naeper
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include <stdlib.h>

#include "dap_test.h"
#include "dap_common.h"
#include "dap_enc_key.h"
#include "dap_sign.h"
#include "dap_enc_mldsa.h"
#include "dap_enc_mlkem.h"
#include "dap_enc_chacha20_poly1305.h"
#include "dap_enc_ntru_prime.h"
#include "dap_enc_ntru_prime_sig.h"
#include "dap_ntru_prime_sig.h"
#include "dap_enc_aes.h"

/* ===== ML-DSA (FIPS 204) ===== */

static void s_test_mldsa_sign_verify(uint8_t a_security_level, const char *a_level_name)
{
    dap_enc_key_t *l_key = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_SIG_ML_DSA, NULL, 0, NULL, 0, a_security_level);
    dap_assert(l_key != NULL, "ML-DSA keygen succeeded");
    dap_assert(l_key->priv_key_data != NULL, "ML-DSA private key allocated");
    dap_assert(l_key->pub_key_data != NULL, "ML-DSA public key allocated");

    const char *l_msg = "ML-DSA test message for signature verification";
    size_t l_msg_len = strlen(l_msg);

    size_t l_sig_size = sizeof(dilithium_signature_t);
    uint8_t *l_sig = DAP_NEW_Z_SIZE(uint8_t, l_sig_size);
    dap_assert(l_sig != NULL, "signature buffer allocated");

    int l_rc = l_key->sign_get(l_key, l_msg, l_msg_len, l_sig, l_sig_size);
    dap_assert(l_rc == 0, "ML-DSA sign succeeded");

    l_rc = l_key->sign_verify(l_key, l_msg, l_msg_len, l_sig, l_sig_size);
    dap_assert(l_rc == 0, "ML-DSA verify succeeded");

    l_sig[0] ^= 0xFF;
    l_rc = l_key->sign_verify(l_key, l_msg, l_msg_len, l_sig, l_sig_size);
    dap_assert(l_rc != 0, "ML-DSA verify rejects corrupted signature");

    DAP_DELETE(l_sig);
    dap_enc_key_delete(l_key);
}

static void s_test_mldsa(void)
{
    dap_print_module_name("ML-DSA (FIPS 204)");

    dap_print_module_name("ML-DSA-44 (Category 2)");
    s_test_mldsa_sign_verify(DAP_SIGN_PARAMS_SECURITY_2, "ML-DSA-44");

    dap_print_module_name("ML-DSA-65 (Category 3, default)");
    s_test_mldsa_sign_verify(DAP_SIGN_PARAMS_DEFAULT, "ML-DSA-65 (default)");
    s_test_mldsa_sign_verify(DAP_SIGN_PARAMS_SECURITY_3, "ML-DSA-65 (explicit)");

    dap_print_module_name("ML-DSA-87 (Category 5)");
    s_test_mldsa_sign_verify(DAP_SIGN_PARAMS_SECURITY_5, "ML-DSA-87");
}

/* ===== ML-KEM (FIPS 203) ===== */

static void s_test_mlkem(void)
{
    dap_print_module_name("ML-KEM (FIPS 203)");

    dap_enc_key_t *l_alice = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_ML_KEM, NULL, 0, NULL, 0, DAP_SIGN_PARAMS_DEFAULT);
    dap_assert(l_alice != NULL, "ML-KEM Alice keygen succeeded");
    dap_assert(l_alice->pub_key_data != NULL, "ML-KEM Alice public key allocated");

    dap_enc_key_t *l_bob = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_ML_KEM, NULL, 0, NULL, 0, DAP_SIGN_PARAMS_DEFAULT);
    dap_assert(l_bob != NULL, "ML-KEM Bob keygen succeeded");

    void *l_cypher_msg = NULL;
    size_t l_ct_len = l_bob->gen_bob_shared_key(
            l_bob, l_alice->pub_key_data, l_alice->pub_key_data_size, &l_cypher_msg);
    dap_assert(l_ct_len > 0, "ML-KEM Bob encaps produced ciphertext");
    dap_assert(l_cypher_msg != NULL, "ML-KEM cipher message created");
    dap_assert(l_bob->shared_key != NULL, "ML-KEM Bob shared secret stored");

    size_t l_alice_ss_len = l_alice->gen_alice_shared_key(
            l_alice, NULL, l_ct_len, (uint8_t *)l_cypher_msg);
    dap_assert(l_alice_ss_len > 0, "ML-KEM Alice shared key derived");

    dap_assert(l_bob->shared_key_size == l_alice->shared_key_size,
               "ML-KEM shared key sizes match");
    dap_assert(memcmp(l_bob->shared_key, l_alice->shared_key,
                      l_bob->shared_key_size) == 0,
               "ML-KEM shared secrets match (Alice == Bob)");

    DAP_DELETE(l_cypher_msg);
    dap_enc_key_delete(l_alice);
    dap_enc_key_delete(l_bob);
}

/* ===== ChaCha20-Poly1305 (RFC 8439) ===== */

static void s_test_chacha20_poly1305_roundtrip(void)
{
    dap_print_module_name("ChaCha20-Poly1305 roundtrip");

    dap_enc_key_t *l_key = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_CHACHA20_POLY1305, NULL, 0, NULL, 0, 0);
    dap_assert(l_key != NULL, "ChaCha20 keygen succeeded");

    const char *l_plaintext = "The quick brown fox jumps over the lazy dog, "
                              "testing ChaCha20-Poly1305 authenticated encryption.";
    size_t l_pt_len = strlen(l_plaintext);
    void *l_ct = NULL;
    size_t l_ct_len = dap_enc_chacha20_poly1305_encrypt(l_key, l_plaintext, l_pt_len, &l_ct);
    dap_assert(l_ct_len > l_pt_len, "ciphertext longer than plaintext (nonce + tag)");
    dap_assert(l_ct != NULL, "ciphertext allocated");

    void *l_dec = NULL;
    size_t l_dec_len = dap_enc_chacha20_poly1305_decrypt(l_key, l_ct, l_ct_len, &l_dec);
    dap_assert(l_dec_len == l_pt_len, "decrypted length matches plaintext");
    dap_assert(memcmp(l_dec, l_plaintext, l_pt_len) == 0,
               "decrypted data matches plaintext");

    ((uint8_t *)l_ct)[l_ct_len / 2] ^= 0x01;
    void *l_bad = NULL;
    size_t l_bad_len = dap_enc_chacha20_poly1305_decrypt(l_key, l_ct, l_ct_len, &l_bad);
    dap_assert(l_bad_len == 0 || l_bad == NULL,
               "tampered ciphertext rejected by Poly1305 tag");

    DAP_DELETE(l_ct);
    DAP_DELETE(l_dec);
    if (l_bad) DAP_DELETE(l_bad);
    dap_enc_key_delete(l_key);
}

static void s_test_chacha20_poly1305_empty(void)
{
    dap_print_module_name("ChaCha20-Poly1305 empty message");

    dap_enc_key_t *l_key = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_CHACHA20_POLY1305, NULL, 0, NULL, 0, 0);
    dap_assert(l_key != NULL, "ChaCha20 keygen succeeded");

    void *l_ct = NULL;
    size_t l_ct_len = dap_enc_chacha20_poly1305_encrypt(l_key, "", 0, &l_ct);
    dap_assert(l_ct_len > 0, "empty plaintext produces nonce + tag");

    void *l_dec = NULL;
    size_t l_dec_len = dap_enc_chacha20_poly1305_decrypt(l_key, l_ct, l_ct_len, &l_dec);
    dap_assert(l_dec_len == 0, "decrypted empty message has length 0");

    DAP_DELETE(l_ct);
    if (l_dec) DAP_DELETE(l_dec);
    dap_enc_key_delete(l_key);
}

static void s_test_chacha20_poly1305(void)
{
    s_test_chacha20_poly1305_roundtrip();
    s_test_chacha20_poly1305_empty();
}

/* ===== NTRU Prime (sntrup761) ===== */

static void s_test_ntru_prime(void)
{
    dap_print_module_name("NTRU Prime (sntrup761)");

    dap_enc_key_t *l_alice = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_KEM_NTRU_PRIME, NULL, 0, NULL, 0, 0);
    dap_assert(l_alice != NULL, "NTRU Prime Alice keygen succeeded");
    dap_assert(l_alice->pub_key_data != NULL, "NTRU Prime Alice public key allocated");

    dap_enc_key_t *l_bob = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_KEM_NTRU_PRIME, NULL, 0, NULL, 0, 0);
    dap_assert(l_bob != NULL, "NTRU Prime Bob keygen succeeded");

    void *l_cypher_msg = NULL;
    size_t l_ct_len = l_bob->gen_bob_shared_key(
            l_bob, l_alice->pub_key_data, l_alice->pub_key_data_size, &l_cypher_msg);
    dap_assert(l_ct_len > 0, "NTRU Prime Bob encaps produced ciphertext");
    dap_assert(l_cypher_msg != NULL, "NTRU Prime cipher message created");

    size_t l_alice_ss_len = l_alice->gen_alice_shared_key(
            l_alice, NULL, l_ct_len, (uint8_t *)l_cypher_msg);
    dap_assert(l_alice_ss_len > 0, "NTRU Prime Alice shared key derived");

    dap_assert(l_bob->shared_key_size == l_alice->shared_key_size,
               "NTRU Prime shared key sizes match");
    dap_assert(memcmp(l_bob->shared_key, l_alice->shared_key,
                      l_bob->shared_key_size) == 0,
               "NTRU Prime shared secrets match (Alice == Bob)");

    DAP_DELETE(l_cypher_msg);
    dap_enc_key_delete(l_alice);
    dap_enc_key_delete(l_bob);
}

/* ===== NTRU Prime Signature ===== */

static void s_test_ntru_prime_sig(void)
{
    dap_print_module_name("NTRU Prime Signature (FSwA, p=761, q=131071)");

    uint8_t pk[NTRU_PRIME_SIG_PUBLICKEYBYTES];
    uint8_t sk[NTRU_PRIME_SIG_SECRETKEYBYTES];
    int l_rc = ntru_prime_sig_keypair(pk, sk);
    dap_assert(l_rc == 0, "NTRU Prime sig keygen succeeded");

    const char *l_msg = "NTRU Prime signature test message";
    size_t l_msg_len = strlen(l_msg);

    uint8_t sig[NTRU_PRIME_SIG_BYTES];
    size_t sig_len = 0;
    l_rc = ntru_prime_sig_sign(sig, &sig_len, (const uint8_t *)l_msg, l_msg_len, sk);
    dap_assert(l_rc == 0, "NTRU Prime sig sign succeeded");
    dap_assert(sig_len == NTRU_PRIME_SIG_BYTES, "NTRU Prime sig size correct");

    l_rc = ntru_prime_sig_verify(sig, sig_len, (const uint8_t *)l_msg, l_msg_len, pk);
    dap_assert(l_rc == 0, "NTRU Prime sig verify succeeded");

    sig[NTRU_PRIME_SIG_SEED_BYTES + 10] ^= 0xFF;
    l_rc = ntru_prime_sig_verify(sig, sig_len, (const uint8_t *)l_msg, l_msg_len, pk);
    dap_assert(l_rc != 0, "NTRU Prime sig verify rejects corrupted signature");

    dap_print_module_name("NTRU Prime Signature via DAP API");

    dap_enc_key_t *l_key = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_SIG_NTRU_PRIME, NULL, 0, NULL, 0, 0);
    dap_assert(l_key != NULL, "NTRU Prime sig DAP keygen succeeded");
    dap_assert(l_key->priv_key_data != NULL, "NTRU Prime sig private key allocated");
    dap_assert(l_key->pub_key_data != NULL, "NTRU Prime sig public key allocated");

    uint8_t l_sig_buf[NTRU_PRIME_SIG_BYTES];
    l_rc = l_key->sign_get(l_key, l_msg, l_msg_len, l_sig_buf, sizeof(l_sig_buf));
    dap_assert(l_rc == 0, "NTRU Prime sig DAP sign succeeded");

    l_rc = l_key->sign_verify(l_key, l_msg, l_msg_len, l_sig_buf, NTRU_PRIME_SIG_BYTES);
    dap_assert(l_rc == 0, "NTRU Prime sig DAP verify succeeded");

    dap_enc_key_delete(l_key);
}

/* ===== Unified AES-256-CBC ===== */

static void s_test_aes256_cbc(void)
{
    dap_print_module_name("Unified AES-256-CBC");

    dap_enc_key_t *l_key = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_AES256_CBC, NULL, 0, NULL, 0, 0);
    dap_assert(l_key != NULL, "AES-256-CBC keygen succeeded");

    const char *l_plaintext = "AES-256-CBC encryption test with unified wrapper.";
    size_t l_pt_len = strlen(l_plaintext);

    void *l_ct = NULL;
    size_t l_ct_len = l_key->enc(l_key, l_plaintext, l_pt_len, &l_ct);
    dap_assert(l_ct_len > 0, "AES-256-CBC encrypt produced output");
    dap_assert(l_ct != NULL, "AES-256-CBC ciphertext allocated");

    void *l_dec = NULL;
    size_t l_dec_len = l_key->dec(l_key, l_ct, l_ct_len, &l_dec);
    dap_assert(l_dec_len == l_pt_len, "AES-256-CBC decrypted length matches");
    dap_assert(memcmp(l_dec, l_plaintext, l_pt_len) == 0,
               "AES-256-CBC decrypted data matches plaintext");

    size_t l_enc_size = dap_enc_aes256_cbc_calc_encode_size(l_pt_len);
    dap_assert(l_enc_size >= l_pt_len, "encode size >= plaintext size");
    size_t l_dec_size = dap_enc_aes256_cbc_calc_decode_size(l_ct_len);
    dap_assert(l_dec_size >= l_pt_len, "decode size >= plaintext size");

    uint8_t *l_ct_fast = DAP_NEW_Z_SIZE(uint8_t, l_enc_size);
    size_t l_ct_fast_len = l_key->enc_na(l_key, l_plaintext, l_pt_len, l_ct_fast, l_enc_size);
    dap_assert(l_ct_fast_len > 0, "AES-256-CBC encrypt_fast produced output");

    uint8_t *l_dec_fast = DAP_NEW_Z_SIZE(uint8_t, l_dec_size);
    size_t l_dec_fast_len = l_key->dec_na(l_key, l_ct_fast, l_ct_fast_len, l_dec_fast, l_dec_size);
    dap_assert(l_dec_fast_len == l_pt_len, "AES-256-CBC decrypt_fast length matches");
    dap_assert(memcmp(l_dec_fast, l_plaintext, l_pt_len) == 0,
               "AES-256-CBC decrypt_fast data matches plaintext");

    DAP_DELETE(l_ct);
    DAP_DELETE(l_dec);
    DAP_DELETE(l_ct_fast);
    DAP_DELETE(l_dec_fast);
    dap_enc_key_delete(l_key);
}

/* ===== Integration: ML-KEM + ChaCha20 + ML-DSA ===== */

static void s_test_integration_pipeline(void)
{
    dap_print_module_name("Integration: ML-KEM encaps + ChaCha20 encrypt + ML-DSA sign");

    dap_enc_key_t *l_kem_alice = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_ML_KEM, NULL, 0, NULL, 0, DAP_SIGN_PARAMS_DEFAULT);
    dap_enc_key_t *l_kem_bob = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_ML_KEM, NULL, 0, NULL, 0, DAP_SIGN_PARAMS_DEFAULT);
    dap_assert(l_kem_alice && l_kem_bob, "KEM keys generated");

    void *l_cypher = NULL;
    size_t l_ct_len = l_kem_bob->gen_bob_shared_key(l_kem_bob, l_kem_alice->pub_key_data,
                                  l_kem_alice->pub_key_data_size, &l_cypher);
    l_kem_alice->gen_alice_shared_key(l_kem_alice, NULL,
                                      l_ct_len, (uint8_t *)l_cypher);
    dap_assert(memcmp(l_kem_bob->shared_key, l_kem_alice->shared_key,
                      l_kem_bob->shared_key_size) == 0,
               "Pipeline: KEM shared secrets match");

    dap_enc_key_t *l_chacha = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_CHACHA20_POLY1305, NULL, 0,
            l_kem_alice->shared_key, l_kem_alice->shared_key_size, 0);
    dap_assert(l_chacha != NULL, "Pipeline: ChaCha20 key from shared secret");

    const char *l_msg = "Confidential message encrypted and signed via PQ pipeline";
    size_t l_msg_len = strlen(l_msg);

    void *l_ct = NULL;
    size_t l_ct_enc_len = dap_enc_chacha20_poly1305_encrypt(l_chacha, l_msg, l_msg_len, &l_ct);
    dap_assert(l_ct_enc_len > 0, "Pipeline: ChaCha20 encryption succeeded");

    dap_enc_key_t *l_sig_key = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_SIG_ML_DSA, NULL, 0, NULL, 0, DAP_SIGN_PARAMS_DEFAULT);
    dap_assert(l_sig_key != NULL, "Pipeline: ML-DSA keygen succeeded");

    size_t l_sig_size = sizeof(dilithium_signature_t);
    uint8_t *l_sig = DAP_NEW_Z_SIZE(uint8_t, l_sig_size);
    int l_rc = l_sig_key->sign_get(l_sig_key, l_ct, l_ct_enc_len, l_sig, l_sig_size);
    dap_assert(l_rc == 0, "Pipeline: ML-DSA sign over ciphertext succeeded");

    l_rc = l_sig_key->sign_verify(l_sig_key, l_ct, l_ct_enc_len, l_sig, l_sig_size);
    dap_assert(l_rc == 0, "Pipeline: ML-DSA verify over ciphertext succeeded");

    void *l_dec = NULL;
    size_t l_dec_len = dap_enc_chacha20_poly1305_decrypt(l_chacha, l_ct, l_ct_enc_len, &l_dec);
    dap_assert(l_dec_len == l_msg_len, "Pipeline: ChaCha20 decryption length matches");
    dap_assert(memcmp(l_dec, l_msg, l_msg_len) == 0,
               "Pipeline: decrypted message matches original");

    DAP_DELETE(l_cypher);
    DAP_DELETE(l_ct);
    DAP_DELETE(l_sig);
    DAP_DELETE(l_dec);
    dap_enc_key_delete(l_kem_alice);
    dap_enc_key_delete(l_kem_bob);
    dap_enc_key_delete(l_chacha);
    dap_enc_key_delete(l_sig_key);
}

/* ===== Benchmarks ===== */

static dap_enc_key_t *s_bench_mldsa_key = NULL;
static dap_enc_key_t *s_bench_chacha_key = NULL;

static void s_bench_mldsa_sign(void)
{
    static const char l_msg[] = "benchmark message for ML-DSA sign";
    uint8_t l_sig[4096];
    s_bench_mldsa_key->sign_get(s_bench_mldsa_key, l_msg, sizeof(l_msg) - 1,
                                l_sig, sizeof(l_sig));
}

static void s_bench_chacha20_encrypt(void)
{
    static const uint8_t l_data[1024] = {0};
    void *l_out = NULL;
    size_t l_len = dap_enc_chacha20_poly1305_encrypt(s_bench_chacha_key,
            l_data, sizeof(l_data), &l_out);
    if (l_out) DAP_DELETE(l_out);
    (void)l_len;
}

/* ===== Main ===== */

int main(void)
{
    dap_print_module_name("PQC & New Symmetric Algorithms");

    s_test_mldsa();
    s_test_mlkem();
    s_test_chacha20_poly1305();
    s_test_ntru_prime();
    s_test_ntru_prime_sig();
    s_test_aes256_cbc();
    s_test_integration_pipeline();

    dap_print_module_name("PQC Benchmarks");

    s_bench_mldsa_key = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_SIG_ML_DSA, NULL, 0, NULL, 0, DAP_SIGN_PARAMS_DEFAULT);
    s_bench_chacha_key = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_CHACHA20_POLY1305, NULL, 0, NULL, 0, 0);

    benchmark_mgs_time("ML-DSA-65 sign", benchmark_test_time(s_bench_mldsa_sign, 100));
    benchmark_mgs_time("ChaCha20 encrypt 1KB", benchmark_test_time(s_bench_chacha20_encrypt, 10000));

    dap_enc_key_delete(s_bench_mldsa_key);
    dap_enc_key_delete(s_bench_chacha_key);

    return 0;
}
