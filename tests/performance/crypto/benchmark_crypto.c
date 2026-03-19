/**
 * @file benchmark_crypto.c
 * @brief Competitive benchmarks: ML-KEM, ML-DSA, ChaCha20-Poly1305, AES-256-CBC, NTRU Prime.
 *
 * Compares DAP SDK implementations against liboqs, OpenSSL, and libsodium.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>

#include "dap_common.h"
#include "dap_enc_key.h"
#include "dap_sign.h"

#ifdef HAVE_LIBOQS
#include <oqs/oqs.h>
#endif

#ifdef HAVE_OPENSSL
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

#ifdef HAVE_LIBSODIUM
#include <sodium.h>
#endif

/* ---- timing helpers ---- */

static inline uint64_t s_rdtsc_or_clock(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

#define BENCH_WARMUP  3
#define BENCH_ITERS   100

typedef struct {
    const char *name;
    double us_per_op;
    double ops_per_sec;
} bench_result_t;

static void s_print_header(const char *a_title)
{
    printf("\n=== %s ===\n", a_title);
    printf("%-40s %12s %12s\n", "Implementation", "us/op", "ops/sec");
    printf("%-40s %12s %12s\n", "----------------------------------------", "------------", "------------");
}

static void s_print_result(const bench_result_t *r)
{
    printf("%-40s %12.2f %12.0f\n", r->name, r->us_per_op, r->ops_per_sec);
}

/* ---- ML-KEM benchmark ---- */

static bench_result_t s_bench_mlkem_dap(uint8_t a_level, const char *a_name)
{
    bench_result_t l_res = { .name = a_name };
    uint64_t l_total = 0;

    for (int w = 0; w < BENCH_WARMUP; w++) {
        dap_enc_key_t *l_a = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_ML_KEM, NULL, 0, NULL, 0, a_level);
        dap_enc_key_t *l_b = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_ML_KEM, NULL, 0, NULL, 0, a_level);
        void *ct = NULL;
        l_b->gen_bob_shared_key(l_b, l_a->pub_key_data, l_a->pub_key_data_size, &ct);
        l_a->gen_alice_shared_key(l_a, NULL, l_b->shared_key_size, ct);
        DAP_DELETE(ct);
        dap_enc_key_delete(l_a);
        dap_enc_key_delete(l_b);
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        dap_enc_key_t *l_a = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_ML_KEM, NULL, 0, NULL, 0, a_level);
        dap_enc_key_t *l_b = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_ML_KEM, NULL, 0, NULL, 0, a_level);
        void *ct = NULL;

        uint64_t t0 = s_rdtsc_or_clock();
        l_b->gen_bob_shared_key(l_b, l_a->pub_key_data, l_a->pub_key_data_size, &ct);
        l_a->gen_alice_shared_key(l_a, NULL, l_b->shared_key_size, ct);
        uint64_t t1 = s_rdtsc_or_clock();

        l_total += t1 - t0;
        DAP_DELETE(ct);
        dap_enc_key_delete(l_a);
        dap_enc_key_delete(l_b);
    }

    double us = (double)l_total / BENCH_ITERS / 1000.0;
    l_res.us_per_op = us;
    l_res.ops_per_sec = 1000000.0 / us;
    return l_res;
}

#ifdef HAVE_LIBOQS
static bench_result_t s_bench_mlkem_oqs(const char *a_alg_name, const char *a_label)
{
    bench_result_t l_res = { .name = a_label };
    OQS_KEM *kem = OQS_KEM_new(a_alg_name);
    if (!kem) {
        l_res.us_per_op = -1;
        return l_res;
    }
    uint8_t *pk = malloc(kem->length_public_key);
    uint8_t *sk = malloc(kem->length_secret_key);
    uint8_t *ct = malloc(kem->length_ciphertext);
    uint8_t *ss_enc = malloc(kem->length_shared_secret);
    uint8_t *ss_dec = malloc(kem->length_shared_secret);

    for (int w = 0; w < BENCH_WARMUP; w++) {
        OQS_KEM_keypair(kem, pk, sk);
        OQS_KEM_encaps(kem, ct, ss_enc, pk);
        OQS_KEM_decaps(kem, ss_dec, ct, sk);
    }

    uint64_t l_total = 0;
    for (int i = 0; i < BENCH_ITERS; i++) {
        OQS_KEM_keypair(kem, pk, sk);
        uint64_t t0 = s_rdtsc_or_clock();
        OQS_KEM_encaps(kem, ct, ss_enc, pk);
        OQS_KEM_decaps(kem, ss_dec, ct, sk);
        uint64_t t1 = s_rdtsc_or_clock();
        l_total += t1 - t0;
    }

    double us = (double)l_total / BENCH_ITERS / 1000.0;
    l_res.us_per_op = us;
    l_res.ops_per_sec = 1000000.0 / us;

    free(pk); free(sk); free(ct); free(ss_enc); free(ss_dec);
    OQS_KEM_free(kem);
    return l_res;
}
#endif

static void s_benchmark_mlkem(void)
{
    s_print_header("ML-KEM (encaps + decaps)");

    bench_result_t r;
    r = s_bench_mlkem_dap(DAP_SIGN_PARAMS_SECURITY_2, "DAP ML-KEM-512");
    s_print_result(&r);
    r = s_bench_mlkem_dap(DAP_SIGN_PARAMS_SECURITY_3, "DAP ML-KEM-768");
    s_print_result(&r);
    r = s_bench_mlkem_dap(DAP_SIGN_PARAMS_SECURITY_5, "DAP ML-KEM-1024");
    s_print_result(&r);

#ifdef HAVE_LIBOQS
    r = s_bench_mlkem_oqs(OQS_KEM_alg_kyber_512, "liboqs ML-KEM-512");
    if (r.us_per_op >= 0) s_print_result(&r);
    r = s_bench_mlkem_oqs(OQS_KEM_alg_kyber_768, "liboqs ML-KEM-768");
    if (r.us_per_op >= 0) s_print_result(&r);
    r = s_bench_mlkem_oqs(OQS_KEM_alg_kyber_1024, "liboqs ML-KEM-1024");
    if (r.us_per_op >= 0) s_print_result(&r);
#else
    printf("  (liboqs not available — run download_competitors.sh)\n");
#endif
}

/* ---- ML-DSA benchmark ---- */

static bench_result_t s_bench_mldsa_dap(uint8_t a_level, const char *a_name)
{
    bench_result_t l_res = { .name = a_name };
    const uint8_t l_msg[] = "Benchmark message for ML-DSA signing test.";
    size_t l_msg_len = sizeof(l_msg) - 1;

    for (int w = 0; w < BENCH_WARMUP; w++) {
        dap_enc_key_t *k = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_ML_DSA, NULL, 0, NULL, 0, a_level);
        if (!k) { l_res.us_per_op = -1; return l_res; }
        dap_sign_t *sig = dap_sign_create(k, l_msg, l_msg_len);
        if (sig) {
            dap_sign_verify(sig, l_msg, l_msg_len);
            DAP_DELETE(sig);
        }
        dap_enc_key_delete(k);
    }

    uint64_t l_total = 0;
    for (int i = 0; i < BENCH_ITERS; i++) {
        dap_enc_key_t *k = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_ML_DSA, NULL, 0, NULL, 0, a_level);
        dap_sign_t *sig = dap_sign_create(k, l_msg, l_msg_len);
        uint64_t t0 = s_rdtsc_or_clock();
        int rc = dap_sign_verify(sig, l_msg, l_msg_len);
        uint64_t t1 = s_rdtsc_or_clock();
        (void)rc;
        l_total += t1 - t0;
        DAP_DELETE(sig);
        dap_enc_key_delete(k);
    }

    double us = (double)l_total / BENCH_ITERS / 1000.0;
    l_res.us_per_op = us;
    l_res.ops_per_sec = 1000000.0 / us;
    return l_res;
}

#ifdef HAVE_LIBOQS
static bench_result_t s_bench_mldsa_oqs(const char *a_alg_name, const char *a_label)
{
    bench_result_t l_res = { .name = a_label };
    OQS_SIG *sig = OQS_SIG_new(a_alg_name);
    if (!sig) { l_res.us_per_op = -1; return l_res; }

    uint8_t *pk = malloc(sig->length_public_key);
    uint8_t *sk = malloc(sig->length_secret_key);
    uint8_t *signature = malloc(sig->length_signature);
    size_t sig_len = 0;
    const uint8_t msg[] = "Benchmark message for ML-DSA signing test.";
    size_t msg_len = sizeof(msg) - 1;

    for (int w = 0; w < BENCH_WARMUP; w++) {
        OQS_SIG_keypair(sig, pk, sk);
        OQS_SIG_sign(sig, signature, &sig_len, msg, msg_len, sk);
        OQS_SIG_verify(sig, msg, msg_len, signature, sig_len, pk);
    }

    uint64_t l_total = 0;
    for (int i = 0; i < BENCH_ITERS; i++) {
        OQS_SIG_keypair(sig, pk, sk);
        OQS_SIG_sign(sig, signature, &sig_len, msg, msg_len, sk);
        uint64_t t0 = s_rdtsc_or_clock();
        OQS_SIG_verify(sig, msg, msg_len, signature, sig_len, pk);
        uint64_t t1 = s_rdtsc_or_clock();
        l_total += t1 - t0;
    }

    double us = (double)l_total / BENCH_ITERS / 1000.0;
    l_res.us_per_op = us;
    l_res.ops_per_sec = 1000000.0 / us;

    free(pk); free(sk); free(signature);
    OQS_SIG_free(sig);
    return l_res;
}
#endif

static void s_benchmark_mldsa(void)
{
    s_print_header("ML-DSA (verify)");

    bench_result_t r;
    r = s_bench_mldsa_dap(DAP_SIGN_PARAMS_SECURITY_2, "DAP ML-DSA-44");
    if (r.us_per_op >= 0) s_print_result(&r);
    r = s_bench_mldsa_dap(DAP_SIGN_PARAMS_SECURITY_3, "DAP ML-DSA-65");
    if (r.us_per_op >= 0) s_print_result(&r);
    r = s_bench_mldsa_dap(DAP_SIGN_PARAMS_SECURITY_5, "DAP ML-DSA-87");
    if (r.us_per_op >= 0) s_print_result(&r);

#ifdef HAVE_LIBOQS
    r = s_bench_mldsa_oqs(OQS_SIG_alg_dilithium_2, "liboqs ML-DSA-44");
    if (r.us_per_op >= 0) s_print_result(&r);
    r = s_bench_mldsa_oqs(OQS_SIG_alg_dilithium_3, "liboqs ML-DSA-65");
    if (r.us_per_op >= 0) s_print_result(&r);
    r = s_bench_mldsa_oqs(OQS_SIG_alg_dilithium_5, "liboqs ML-DSA-87");
    if (r.us_per_op >= 0) s_print_result(&r);
#else
    printf("  (liboqs not available)\n");
#endif
}

/* ---- ChaCha20-Poly1305 benchmark ---- */

static void s_benchmark_chacha20(void)
{
    s_print_header("ChaCha20-Poly1305 (encrypt 4096 bytes)");

    const size_t MSG_LEN = 4096;
    uint8_t *l_msg = calloc(1, MSG_LEN);
    uint8_t *l_out = calloc(1, MSG_LEN + 64);

    dap_enc_key_t *l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_CHACHA20_POLY1305,
                                                      NULL, 0, NULL, 0, 0);
    if (!l_key) {
        printf("  (ChaCha20-Poly1305 keygen failed)\n");
        free(l_msg); free(l_out);
        return;
    }

    dap_enc_callback_dataop_na_t l_enc_fn = l_key->enc_na;
    int l_use_alloc = 0;
    if (!l_enc_fn) {
        if (!l_key->enc) {
            printf("  (ChaCha20-Poly1305 not available)\n");
            dap_enc_key_delete(l_key); free(l_msg); free(l_out);
            return;
        }
        l_use_alloc = 1;
    }

    for (int w = 0; w < BENCH_WARMUP; w++) {
        if (l_use_alloc) {
            void *tmp = NULL;
            l_key->enc(l_key, l_msg, MSG_LEN, &tmp);
            DAP_DELETE(tmp);
        } else {
            l_enc_fn(l_key, l_msg, MSG_LEN, l_out, MSG_LEN + 64);
        }
    }

    uint64_t l_total = 0;
    int iters = BENCH_ITERS * 10;
    for (int i = 0; i < iters; i++) {
        uint64_t t0 = s_rdtsc_or_clock();
        if (l_use_alloc) {
            void *tmp = NULL;
            l_key->enc(l_key, l_msg, MSG_LEN, &tmp);
            DAP_DELETE(tmp);
        } else {
            l_enc_fn(l_key, l_msg, MSG_LEN, l_out, MSG_LEN + 64);
        }
        uint64_t t1 = s_rdtsc_or_clock();
        l_total += t1 - t0;
    }
    double us = (double)l_total / iters / 1000.0;
    bench_result_t r = { .name = "DAP ChaCha20-Poly1305", .us_per_op = us, .ops_per_sec = 1000000.0 / us };
    s_print_result(&r);
    double mbps = (double)MSG_LEN * iters / ((double)l_total / 1e9) / (1024 * 1024);
    printf("  Throughput: %.1f MB/s\n", mbps);

    dap_enc_key_delete(l_key);

#ifdef HAVE_OPENSSL
    {
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        uint8_t key[32], nonce[12], tag[16];
        RAND_bytes(key, 32);
        RAND_bytes(nonce, 12);
        int outl = 0;
        l_total = 0;
        for (int i = 0; i < iters; i++) {
            EVP_EncryptInit_ex(ctx, EVP_chacha20_poly1305(), NULL, key, nonce);
            uint64_t t0 = s_rdtsc_or_clock();
            EVP_EncryptUpdate(ctx, l_out, &outl, l_msg, MSG_LEN);
            EVP_EncryptFinal_ex(ctx, l_out + outl, &outl);
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16, tag);
            uint64_t t1 = s_rdtsc_or_clock();
            l_total += t1 - t0;
        }
        us = (double)l_total / iters / 1000.0;
        r = (bench_result_t){ .name = "OpenSSL ChaCha20-Poly1305", .us_per_op = us, .ops_per_sec = 1000000.0 / us };
        s_print_result(&r);
        mbps = (double)MSG_LEN * iters / ((double)l_total / 1e9) / (1024 * 1024);
        printf("  Throughput: %.1f MB/s\n", mbps);
        EVP_CIPHER_CTX_free(ctx);
    }
#else
    printf("  (OpenSSL not available)\n");
#endif

#ifdef HAVE_LIBSODIUM
    {
        uint8_t key[crypto_aead_chacha20poly1305_ietf_KEYBYTES];
        uint8_t nonce[crypto_aead_chacha20poly1305_ietf_NPUBBYTES];
        unsigned long long clen;
        randombytes_buf(key, sizeof(key));
        randombytes_buf(nonce, sizeof(nonce));
        l_total = 0;
        for (int i = 0; i < iters; i++) {
            uint64_t t0 = s_rdtsc_or_clock();
            crypto_aead_chacha20poly1305_ietf_encrypt(l_out, &clen, l_msg, MSG_LEN,
                                                       NULL, 0, NULL, nonce, key);
            uint64_t t1 = s_rdtsc_or_clock();
            l_total += t1 - t0;
        }
        us = (double)l_total / iters / 1000.0;
        r = (bench_result_t){ .name = "libsodium ChaCha20-Poly1305", .us_per_op = us, .ops_per_sec = 1000000.0 / us };
        s_print_result(&r);
        mbps = (double)MSG_LEN * iters / ((double)l_total / 1e9) / (1024 * 1024);
        printf("  Throughput: %.1f MB/s\n", mbps);
    }
#else
    printf("  (libsodium not available)\n");
#endif

    free(l_msg);
    free(l_out);
}

/* ---- AES-256-CBC benchmark ---- */

static void s_benchmark_aes(void)
{
    s_print_header("AES-256-CBC (encrypt 4096 bytes)");

    const size_t MSG_LEN = 4096;
    uint8_t *l_msg = calloc(1, MSG_LEN);
    uint8_t *l_out = calloc(1, MSG_LEN + 32);

    dap_enc_key_t *l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_AES256_CBC, NULL, 0, NULL, 0, 0);
    if (!l_key) {
        printf("  (AES-256-CBC keygen failed)\n");
        free(l_msg); free(l_out);
        return;
    }

    if (!l_key->enc_na) {
        printf("  (AES-256-CBC enc_na not available)\n");
        dap_enc_key_delete(l_key); free(l_msg); free(l_out);
        return;
    }
    for (int w = 0; w < BENCH_WARMUP; w++)
        l_key->enc_na(l_key, l_msg, MSG_LEN, l_out, MSG_LEN + 32);

    int iters = BENCH_ITERS * 10;
    uint64_t l_total = 0;
    for (int i = 0; i < iters; i++) {
        uint64_t t0 = s_rdtsc_or_clock();
        l_key->enc_na(l_key, l_msg, MSG_LEN, l_out, MSG_LEN + 32);
        uint64_t t1 = s_rdtsc_or_clock();
        l_total += t1 - t0;
    }
    double us = (double)l_total / iters / 1000.0;
    bench_result_t r = { .name = "DAP AES-256-CBC", .us_per_op = us, .ops_per_sec = 1000000.0 / us };
    s_print_result(&r);
    double mbps = (double)MSG_LEN * iters / ((double)l_total / 1e9) / (1024 * 1024);
    printf("  Throughput: %.1f MB/s\n", mbps);

    dap_enc_key_delete(l_key);

#ifdef HAVE_OPENSSL
    {
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        uint8_t key[32], iv[16];
        RAND_bytes(key, 32);
        RAND_bytes(iv, 16);
        int outl = 0;
        l_total = 0;
        for (int i = 0; i < iters; i++) {
            EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv);
            uint64_t t0 = s_rdtsc_or_clock();
            EVP_EncryptUpdate(ctx, l_out, &outl, l_msg, MSG_LEN);
            EVP_EncryptFinal_ex(ctx, l_out + outl, &outl);
            uint64_t t1 = s_rdtsc_or_clock();
            l_total += t1 - t0;
        }
        us = (double)l_total / iters / 1000.0;
        r = (bench_result_t){ .name = "OpenSSL AES-256-CBC", .us_per_op = us, .ops_per_sec = 1000000.0 / us };
        s_print_result(&r);
        mbps = (double)MSG_LEN * iters / ((double)l_total / 1e9) / (1024 * 1024);
        printf("  Throughput: %.1f MB/s\n", mbps);
        EVP_CIPHER_CTX_free(ctx);
    }
#else
    printf("  (OpenSSL not available)\n");
#endif

    free(l_msg);
    free(l_out);
}

/* ---- NTRU Prime benchmark ---- */

static void s_benchmark_ntru_prime(void)
{
    s_print_header("NTRU Prime KEM (encaps + decaps)");

    uint64_t l_total = 0;
    for (int w = 0; w < BENCH_WARMUP; w++) {
        dap_enc_key_t *l_a = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_KEM_NTRU_PRIME, NULL, 0, NULL, 0, 0);
        if (!l_a) { printf("  (NTRU Prime not available)\n"); return; }
        dap_enc_key_t *l_b = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_KEM_NTRU_PRIME, NULL, 0, NULL, 0, 0);
        void *ct = NULL;
        l_b->gen_bob_shared_key(l_b, l_a->pub_key_data, l_a->pub_key_data_size, &ct);
        l_a->gen_alice_shared_key(l_a, NULL, l_b->shared_key_size, ct);
        DAP_DELETE(ct);
        dap_enc_key_delete(l_a);
        dap_enc_key_delete(l_b);
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        dap_enc_key_t *l_a = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_KEM_NTRU_PRIME, NULL, 0, NULL, 0, 0);
        dap_enc_key_t *l_b = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_KEM_NTRU_PRIME, NULL, 0, NULL, 0, 0);
        void *ct = NULL;
        uint64_t t0 = s_rdtsc_or_clock();
        l_b->gen_bob_shared_key(l_b, l_a->pub_key_data, l_a->pub_key_data_size, &ct);
        l_a->gen_alice_shared_key(l_a, NULL, l_b->shared_key_size, ct);
        uint64_t t1 = s_rdtsc_or_clock();
        l_total += t1 - t0;
        DAP_DELETE(ct);
        dap_enc_key_delete(l_a);
        dap_enc_key_delete(l_b);
    }

    double us = (double)l_total / BENCH_ITERS / 1000.0;
    bench_result_t r = { .name = "DAP NTRU Prime", .us_per_op = us, .ops_per_sec = 1000000.0 / us };
    s_print_result(&r);
}

/* ---- main ---- */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    dap_common_init("benchmark_crypto", NULL);

    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║     DAP SDK Cryptographic Performance Benchmarks         ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");

#ifdef HAVE_LIBOQS
    printf("  [+] liboqs competitor: enabled\n");
#endif
#ifdef HAVE_OPENSSL
    printf("  [+] OpenSSL competitor: enabled\n");
#endif
#ifdef HAVE_LIBSODIUM
    printf("  [+] libsodium competitor: enabled\n");
#endif

    s_benchmark_mlkem();
    s_benchmark_mldsa();
    s_benchmark_chacha20();
    s_benchmark_aes();
    s_benchmark_ntru_prime();

    printf("\n=== Done ===\n");
    dap_common_deinit();
    return 0;
}
