/**
 * @file benchmark_crypto.c
 * @brief Competitive benchmarks: ML-KEM, ML-DSA, ChaCha20-Poly1305, AES-256-CBC, NTRU Prime.
 *
 * Compares DAP SDK implementations against liboqs, OpenSSL, and libsodium.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <inttypes.h>

#include "dap_common.h"
#include "dap_rand.h"
#include "dap_enc_key.h"
#include "dap_sign.h"
#include "dap_chacha20_poly1305.h"
#include "dap_cpu_arch.h"
#include "dap_cpu_detect.h"
#include "dap_hash_keccak.h"
#include "dap_hash_keccak_x4.h"

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

static int s_bench_iters = 5000;
static int s_bench_warmup = 100;
#define BENCH_WARMUP  s_bench_warmup
#define BENCH_ITERS   s_bench_iters
#define BENCH_POOL    32

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

/* ---- ML-KEM raw (no-alloc) benchmark ---- */

int dap_mlkem512_kem_keypair(uint8_t *pk, uint8_t *sk);
int dap_mlkem512_kem_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int dap_mlkem512_kem_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);
int dap_mlkem768_kem_keypair(uint8_t *pk, uint8_t *sk);
int dap_mlkem768_kem_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int dap_mlkem768_kem_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);
int dap_mlkem1024_kem_keypair(uint8_t *pk, uint8_t *sk);
int dap_mlkem1024_kem_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int dap_mlkem1024_kem_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

typedef struct {
    int (*keypair)(uint8_t *, uint8_t *);
    int (*enc)(uint8_t *, uint8_t *, const uint8_t *);
    int (*dec)(uint8_t *, const uint8_t *, const uint8_t *);
    size_t pk, sk, ct, ss;
} s_mlkem_raw_t;

static const s_mlkem_raw_t s_mlkem_raw[] = {
    { dap_mlkem512_kem_keypair, dap_mlkem512_kem_enc, dap_mlkem512_kem_dec,
      800, 1632, 768, 32 },
    { dap_mlkem768_kem_keypair, dap_mlkem768_kem_enc, dap_mlkem768_kem_dec,
      1184, 2400, 1088, 32 },
    { dap_mlkem1024_kem_keypair, dap_mlkem1024_kem_enc, dap_mlkem1024_kem_dec,
      1568, 3168, 1568, 32 },
};

static bench_result_t s_bench_mlkem_raw(int a_variant, const char *a_name)
{
    bench_result_t l_res = { .name = a_name };
    const s_mlkem_raw_t *v = &s_mlkem_raw[a_variant];
    uint8_t *pk = calloc(1, v->pk), *sk = calloc(1, v->sk);
    uint8_t *ct = calloc(1, v->ct), *ss_enc = calloc(1, v->ss), *ss_dec = calloc(1, v->ss);
    v->keypair(pk, sk);

    for (int w = 0; w < BENCH_WARMUP; w++) {
        v->enc(ct, ss_enc, pk);
        v->dec(ss_dec, ct, sk);
    }
    uint64_t l_total = 0;
    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = s_rdtsc_or_clock();
        v->enc(ct, ss_enc, pk);
        v->dec(ss_dec, ct, sk);
        uint64_t t1 = s_rdtsc_or_clock();
        l_total += t1 - t0;
    }
    free(pk); free(sk); free(ct); free(ss_enc); free(ss_dec);
    double us = (double)l_total / BENCH_ITERS / 1000.0;
    l_res.us_per_op = us;
    l_res.ops_per_sec = 1000000.0 / us;
    return l_res;
}

/* ---- ML-KEM benchmark (wrapper) ---- */

static bench_result_t s_bench_mlkem_dap(uint8_t a_level, const char *a_name)
{
    bench_result_t l_res = { .name = a_name };
    dap_enc_key_t *l_keys_a[BENCH_POOL], *l_keys_b[BENCH_POOL];
    for (int j = 0; j < BENCH_POOL; j++) {
        l_keys_a[j] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_ML_KEM, NULL, 0, NULL, 0, a_level);
        l_keys_b[j] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_ML_KEM, NULL, 0, NULL, 0, a_level);
    }

    for (int w = 0; w < BENCH_WARMUP; w++) {
        int j = w % BENCH_POOL;
        void *ct = NULL;
        size_t ct_sz = l_keys_b[j]->gen_bob_shared_key(l_keys_b[j], l_keys_a[j]->pub_key_data,
                                          l_keys_a[j]->pub_key_data_size, &ct);
        l_keys_a[j]->gen_alice_shared_key(l_keys_a[j], NULL, ct_sz, ct);
        DAP_DELETE(ct);
    }

    uint64_t l_total = 0;
    for (int i = 0; i < BENCH_ITERS; i++) {
        int j = i % BENCH_POOL;
        void *ct = NULL;
        uint64_t t0 = s_rdtsc_or_clock();
        size_t ct_sz = l_keys_b[j]->gen_bob_shared_key(l_keys_b[j], l_keys_a[j]->pub_key_data,
                                          l_keys_a[j]->pub_key_data_size, &ct);
        l_keys_a[j]->gen_alice_shared_key(l_keys_a[j], NULL, ct_sz, ct);
        uint64_t t1 = s_rdtsc_or_clock();
        l_total += t1 - t0;
        DAP_DELETE(ct);
    }

    for (int j = 0; j < BENCH_POOL; j++) {
        dap_enc_key_delete(l_keys_a[j]);
        dap_enc_key_delete(l_keys_b[j]);
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
    uint8_t *pk[BENCH_POOL], *sk[BENCH_POOL];
    for (int j = 0; j < BENCH_POOL; j++) {
        pk[j] = DAP_NEW_SIZE(uint8_t, kem->length_public_key);
        sk[j] = DAP_NEW_SIZE(uint8_t, kem->length_secret_key);
        OQS_KEM_keypair(kem, pk[j], sk[j]);
    }
    uint8_t *ct     = DAP_NEW_SIZE(uint8_t, kem->length_ciphertext);
    uint8_t *ss_enc = DAP_NEW_SIZE(uint8_t, kem->length_shared_secret);
    uint8_t *ss_dec = DAP_NEW_SIZE(uint8_t, kem->length_shared_secret);

    for (int w = 0; w < BENCH_WARMUP; w++) {
        int j = w % BENCH_POOL;
        OQS_KEM_encaps(kem, ct, ss_enc, pk[j]);
        OQS_KEM_decaps(kem, ss_dec, ct, sk[j]);
    }

    uint64_t l_total = 0;
    for (int i = 0; i < BENCH_ITERS; i++) {
        int j = i % BENCH_POOL;
        uint64_t t0 = s_rdtsc_or_clock();
        OQS_KEM_encaps(kem, ct, ss_enc, pk[j]);
        OQS_KEM_decaps(kem, ss_dec, ct, sk[j]);
        uint64_t t1 = s_rdtsc_or_clock();
        l_total += t1 - t0;
    }

    double us = (double)l_total / BENCH_ITERS / 1000.0;
    l_res.us_per_op = us;
    l_res.ops_per_sec = 1000000.0 / us;

    for (int j = 0; j < BENCH_POOL; j++) { DAP_DELETE(pk[j]); DAP_DELETE(sk[j]); }
    DAP_DELETE(ct); DAP_DELETE(ss_enc); DAP_DELETE(ss_dec);
    OQS_KEM_free(kem);
    return l_res;
}
#endif

static void s_benchmark_mlkem(void)
{
    s_print_header("ML-KEM (encaps + decaps)");

    bench_result_t r;
    if (!getenv("BENCH_OQS_ONLY")) {
        r = s_bench_mlkem_raw(0, "DAP ML-KEM-512  (raw)");
        s_print_result(&r);
        r = s_bench_mlkem_raw(1, "DAP ML-KEM-768  (raw)");
        s_print_result(&r);
        r = s_bench_mlkem_raw(2, "DAP ML-KEM-1024 (raw)");
        s_print_result(&r);
        r = s_bench_mlkem_dap(DAP_SIGN_PARAMS_SECURITY_2, "DAP ML-KEM-512  (wrap)");
        s_print_result(&r);
        r = s_bench_mlkem_dap(DAP_SIGN_PARAMS_SECURITY_3, "DAP ML-KEM-768  (wrap)");
        s_print_result(&r);
        r = s_bench_mlkem_dap(DAP_SIGN_PARAMS_SECURITY_5, "DAP ML-KEM-1024 (wrap)");
        s_print_result(&r);
    }

#ifdef HAVE_LIBOQS
    if (!getenv("BENCH_DAP_ONLY")) {
        r = s_bench_mlkem_oqs(OQS_KEM_alg_kyber_512, "liboqs ML-KEM-512");
        if (r.us_per_op >= 0) s_print_result(&r);
        r = s_bench_mlkem_oqs(OQS_KEM_alg_kyber_768, "liboqs ML-KEM-768");
        if (r.us_per_op >= 0) s_print_result(&r);
        r = s_bench_mlkem_oqs(OQS_KEM_alg_kyber_1024, "liboqs ML-KEM-1024");
        if (r.us_per_op >= 0) s_print_result(&r);
    }
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

    dap_enc_key_t *l_keys[BENCH_POOL];
    dap_sign_t    *l_sigs[BENCH_POOL];
    int l_valid = 0;
    for (int j = 0; j < BENCH_POOL; j++) {
        l_keys[j] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_ML_DSA, NULL, 0, NULL, 0, a_level);
        if (!l_keys[j]) { l_res.us_per_op = -1; return l_res; }
        l_sigs[j] = dap_sign_create(l_keys[j], l_msg, l_msg_len);
        if (l_sigs[j]) l_valid++;
    }
    if (!l_valid) {
        printf("  (%s: all signatures failed to create)\n", a_name);
        for (int j = 0; j < BENCH_POOL; j++) dap_enc_key_delete(l_keys[j]);
        l_res.us_per_op = -1;
        return l_res;
    }

    for (int w = 0; w < BENCH_WARMUP; w++) {
        int j = w % l_valid;
        if (l_sigs[j]) dap_sign_verify(l_sigs[j], l_msg, l_msg_len);
    }

    uint64_t l_total = 0;
    for (int i = 0; i < BENCH_ITERS; i++) {
        int j = i % l_valid;
        if (!l_sigs[j]) continue;
        uint64_t t0 = s_rdtsc_or_clock();
        dap_sign_verify(l_sigs[j], l_msg, l_msg_len);
        uint64_t t1 = s_rdtsc_or_clock();
        l_total += t1 - t0;
    }

    for (int j = 0; j < BENCH_POOL; j++) {
        DAP_DELETE(l_sigs[j]);
        dap_enc_key_delete(l_keys[j]);
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

    const uint8_t msg[] = "Benchmark message for ML-DSA signing test.";
    size_t msg_len = sizeof(msg) - 1;

    uint8_t *pk[BENCH_POOL], *sk[BENCH_POOL], *signature[BENCH_POOL];
    size_t sig_len[BENCH_POOL];
    for (int j = 0; j < BENCH_POOL; j++) {
        pk[j]        = DAP_NEW_SIZE(uint8_t, sig->length_public_key);
        sk[j]        = DAP_NEW_SIZE(uint8_t, sig->length_secret_key);
        signature[j] = DAP_NEW_SIZE(uint8_t, sig->length_signature);
        OQS_SIG_keypair(sig, pk[j], sk[j]);
        OQS_SIG_sign(sig, signature[j], &sig_len[j], msg, msg_len, sk[j]);
    }

    for (int w = 0; w < BENCH_WARMUP; w++)
        OQS_SIG_verify(sig, msg, msg_len, signature[w % BENCH_POOL],
                        sig_len[w % BENCH_POOL], pk[w % BENCH_POOL]);

    uint64_t l_total = 0;
    for (int i = 0; i < BENCH_ITERS; i++) {
        int j = i % BENCH_POOL;
        uint64_t t0 = s_rdtsc_or_clock();
        OQS_SIG_verify(sig, msg, msg_len, signature[j], sig_len[j], pk[j]);
        uint64_t t1 = s_rdtsc_or_clock();
        l_total += t1 - t0;
    }

    double us = (double)l_total / BENCH_ITERS / 1000.0;
    l_res.us_per_op = us;
    l_res.ops_per_sec = 1000000.0 / us;

    for (int j = 0; j < BENCH_POOL; j++) { DAP_DELETE(pk[j]); DAP_DELETE(sk[j]); DAP_DELETE(signature[j]); }
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
    r = s_bench_mldsa_oqs(OQS_SIG_alg_ml_dsa_44, "liboqs ML-DSA-44");
    if (r.us_per_op >= 0) s_print_result(&r);
    r = s_bench_mldsa_oqs(OQS_SIG_alg_ml_dsa_65, "liboqs ML-DSA-65");
    if (r.us_per_op >= 0) s_print_result(&r);
    r = s_bench_mldsa_oqs(OQS_SIG_alg_ml_dsa_87, "liboqs ML-DSA-87");
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
    uint8_t *l_msgs[BENCH_POOL];
    for (int j = 0; j < BENCH_POOL; j++) {
        l_msgs[j] = DAP_NEW_SIZE(uint8_t, MSG_LEN);
        dap_random_bytes(l_msgs[j], MSG_LEN);
    }
    uint8_t *l_out = DAP_NEW_Z_SIZE(uint8_t, MSG_LEN + 64);

    dap_enc_key_t *l_keys[BENCH_POOL];
    for (int j = 0; j < BENCH_POOL; j++) {
        l_keys[j] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_CHACHA20_POLY1305, NULL, 0, NULL, 0, 0);
        if (!l_keys[j]) {
            printf("  (ChaCha20-Poly1305 keygen failed)\n");
            for (int k = 0; k < j; k++) dap_enc_key_delete(l_keys[k]);
            for (int k = 0; k < BENCH_POOL; k++) DAP_DELETE(l_msgs[k]);
            DAP_DELETE(l_out);
            return;
        }
    }

    uint8_t l_nonce[12], l_tag[16];
    dap_random_bytes(l_nonce, sizeof(l_nonce));

    for (int w = 0; w < BENCH_WARMUP; w++) {
        int j = w % BENCH_POOL;
        dap_chacha20_poly1305_seal(l_out, l_tag, l_msgs[j], MSG_LEN,
                                    NULL, 0, l_keys[j]->priv_key_data, l_nonce);
    }

    uint64_t l_total = 0;
    int iters = BENCH_ITERS * 10;
    for (int i = 0; i < iters; i++) {
        int j = i % BENCH_POOL;
        uint64_t t0 = s_rdtsc_or_clock();
        dap_chacha20_poly1305_seal(l_out, l_tag, l_msgs[j], MSG_LEN,
                                    NULL, 0, l_keys[j]->priv_key_data, l_nonce);
        uint64_t t1 = s_rdtsc_or_clock();
        l_total += t1 - t0;
    }
    double us = (double)l_total / iters / 1000.0;
    bench_result_t r = { .name = "DAP ChaCha20-Poly1305", .us_per_op = us, .ops_per_sec = 1000000.0 / us };
    s_print_result(&r);
    double mbps = (double)MSG_LEN * iters / ((double)l_total / 1e9) / (1024 * 1024);
    printf("  Throughput: %.1f MB/s\n", mbps);

    for (int j = 0; j < BENCH_POOL; j++) dap_enc_key_delete(l_keys[j]);

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
            EVP_EncryptUpdate(ctx, l_out, &outl, l_msgs[i % BENCH_POOL], MSG_LEN);
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
            crypto_aead_chacha20poly1305_ietf_encrypt(l_out, &clen, l_msgs[i % BENCH_POOL], MSG_LEN,
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

    for (int j = 0; j < BENCH_POOL; j++) DAP_DELETE(l_msgs[j]);
    DAP_DELETE(l_out);
}

/* ---- AES-256-CBC benchmark ---- */

static void s_benchmark_aes(void)
{
    s_print_header("AES-256-CBC (encrypt 4096 bytes)");

    const size_t MSG_LEN = 4096;
    uint8_t *l_msgs[BENCH_POOL];
    for (int j = 0; j < BENCH_POOL; j++) {
        l_msgs[j] = DAP_NEW_SIZE(uint8_t, MSG_LEN);
        dap_random_bytes(l_msgs[j], MSG_LEN);
    }
    uint8_t *l_out = DAP_NEW_Z_SIZE(uint8_t, MSG_LEN + 32);

    dap_enc_key_t *l_keys[BENCH_POOL];
    for (int j = 0; j < BENCH_POOL; j++) {
        l_keys[j] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_AES256_CBC, NULL, 0, NULL, 0, 0);
        if (!l_keys[j]) {
            printf("  (AES-256-CBC keygen failed)\n");
            for (int k = 0; k < j; k++) dap_enc_key_delete(l_keys[k]);
            for (int k = 0; k < BENCH_POOL; k++) DAP_DELETE(l_msgs[k]);
            DAP_DELETE(l_out);
            return;
        }
    }

    if (!l_keys[0]->enc_na) {
        printf("  (AES-256-CBC enc_na not available)\n");
        for (int j = 0; j < BENCH_POOL; j++) { dap_enc_key_delete(l_keys[j]); DAP_DELETE(l_msgs[j]); }
        DAP_DELETE(l_out);
        return;
    }
    for (int w = 0; w < BENCH_WARMUP; w++) {
        int j = w % BENCH_POOL;
        l_keys[j]->enc_na(l_keys[j], l_msgs[j], MSG_LEN, l_out, MSG_LEN + 32);
    }

    int iters = BENCH_ITERS * 10;
    uint64_t l_total = 0;
    for (int i = 0; i < iters; i++) {
        int j = i % BENCH_POOL;
        uint64_t t0 = s_rdtsc_or_clock();
        l_keys[j]->enc_na(l_keys[j], l_msgs[j], MSG_LEN, l_out, MSG_LEN + 32);
        uint64_t t1 = s_rdtsc_or_clock();
        l_total += t1 - t0;
    }
    double us = (double)l_total / iters / 1000.0;
    bench_result_t r = { .name = "DAP AES-256-CBC", .us_per_op = us, .ops_per_sec = 1000000.0 / us };
    s_print_result(&r);
    double mbps = (double)MSG_LEN * iters / ((double)l_total / 1e9) / (1024 * 1024);
    printf("  Throughput: %.1f MB/s\n", mbps);

    for (int j = 0; j < BENCH_POOL; j++) dap_enc_key_delete(l_keys[j]);

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
            EVP_EncryptUpdate(ctx, l_out, &outl, l_msgs[i % BENCH_POOL], MSG_LEN);
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

    for (int j = 0; j < BENCH_POOL; j++) DAP_DELETE(l_msgs[j]);
    DAP_DELETE(l_out);
}

/* ---- NTRU Prime benchmark ---- */

static void s_benchmark_ntru_prime(void)
{
    s_print_header("NTRU Prime KEM (encaps + decaps)");

    dap_enc_key_t *l_keys_a[BENCH_POOL], *l_keys_b[BENCH_POOL];
    for (int j = 0; j < BENCH_POOL; j++) {
        l_keys_a[j] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_KEM_NTRU_PRIME, NULL, 0, NULL, 0, 0);
        if (!l_keys_a[j]) { printf("  (NTRU Prime not available)\n"); return; }
        l_keys_b[j] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_KEM_NTRU_PRIME, NULL, 0, NULL, 0, 0);
    }

    for (int w = 0; w < BENCH_WARMUP; w++) {
        int j = w % BENCH_POOL;
        void *ct = NULL;
        size_t ct_sz = l_keys_b[j]->gen_bob_shared_key(l_keys_b[j], l_keys_a[j]->pub_key_data,
                                          l_keys_a[j]->pub_key_data_size, &ct);
        l_keys_a[j]->gen_alice_shared_key(l_keys_a[j], NULL, ct_sz, ct);
        DAP_DELETE(ct);
    }

    uint64_t l_total = 0;
    for (int i = 0; i < BENCH_ITERS; i++) {
        int j = i % BENCH_POOL;
        void *ct = NULL;
        uint64_t t0 = s_rdtsc_or_clock();
        size_t ct_sz = l_keys_b[j]->gen_bob_shared_key(l_keys_b[j], l_keys_a[j]->pub_key_data,
                                          l_keys_a[j]->pub_key_data_size, &ct);
        l_keys_a[j]->gen_alice_shared_key(l_keys_a[j], NULL, ct_sz, ct);
        uint64_t t1 = s_rdtsc_or_clock();
        l_total += t1 - t0;
        DAP_DELETE(ct);
    }

    for (int j = 0; j < BENCH_POOL; j++) {
        dap_enc_key_delete(l_keys_a[j]);
        dap_enc_key_delete(l_keys_b[j]);
    }

    double us = (double)l_total / BENCH_ITERS / 1000.0;
    bench_result_t r = { .name = "DAP NTRU Prime", .us_per_op = us, .ops_per_sec = 1000000.0 / us };
    s_print_result(&r);
}

/* ---- main ---- */

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (getenv("BENCH_QUICK")) {
        s_bench_iters = 50;
        s_bench_warmup = 5;
    }
    dap_common_init("benchmark_crypto", NULL);
    dap_log_level_set(L_WARNING);

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

    {
        dap_cpu_features_t f = dap_cpu_detect_features();
        printf("  CPU: %s\n", dap_cpu_get_name());
        printf("  SIMD arch: %s", dap_cpu_arch_get_name(dap_cpu_arch_get()));
#if DAP_CPU_DETECT_X86
        printf(" (AVX2=%d AVX512F=%d AVX512BW=%d AVX512VL=%d)\n",
               f.has_avx2, f.has_avx512f, f.has_avx512bw, f.has_avx512vl);
#else
        printf("\n");
        (void)f;
#endif
    }

    /* Keccak micro-benchmark */
    if (getenv("BENCH_KECCAK")) {
        const int N = 200000;
        s_print_header("Keccak Permutation (micro)");
        {
            dap_hash_keccak_state_t st = {0};
            for (int i = 0; i < 1000; i++) dap_hash_keccak_permute(&st);
            uint64_t t0 = s_rdtsc_or_clock();
            for (int i = 0; i < N; i++) dap_hash_keccak_permute(&st);
            uint64_t t1 = s_rdtsc_or_clock();
            printf("  dispatch (cached ptr) 1x: %.1f ns\n", (double)(t1-t0)/N);
        }
#if defined(__x86_64__) || defined(__i386__)
        {
            dap_hash_keccak_state_t st = {0};
            for (int i = 0; i < 1000; i++) dap_hash_keccak_permute_avx512(&st);
            uint64_t t0 = s_rdtsc_or_clock();
            for (int i = 0; i < N; i++) dap_hash_keccak_permute_avx512(&st);
            uint64_t t1 = s_rdtsc_or_clock();
            printf("  direct avx512 (plane)  1x: %.1f ns\n", (double)(t1-t0)/N);
        }
        {
            dap_hash_keccak_state_t st = {0};
            for (int i = 0; i < 1000; i++) dap_hash_keccak_permute_avx2(&st);
            uint64_t t0 = s_rdtsc_or_clock();
            for (int i = 0; i < N; i++) dap_hash_keccak_permute_avx2(&st);
            uint64_t t1 = s_rdtsc_or_clock();
            printf("  direct avx2   (lane)   1x: %.1f ns\n", (double)(t1-t0)/N);
        }
        {
            dap_hash_keccak_state_t st = {0};
            for (int i = 0; i < 1000; i++) dap_hash_keccak_permute_ref(&st);
            uint64_t t0 = s_rdtsc_or_clock();
            for (int i = 0; i < N; i++) dap_hash_keccak_permute_ref(&st);
            uint64_t t1 = s_rdtsc_or_clock();
            printf("  direct ref    (scalar) 1x: %.1f ns\n", (double)(t1-t0)/N);
        }
#endif
        {
            dap_keccak_x4_state_t st = {0}; dap_keccak_x4_init(&st);
            for (int i = 0; i < 1000; i++) dap_keccak_x4_permute(&st);
            uint64_t t0 = s_rdtsc_or_clock();
            for (int i = 0; i < N; i++) dap_keccak_x4_permute(&st);
            uint64_t t1 = s_rdtsc_or_clock();
            double ns = (double)(t1-t0)/N;
            printf("  x4 dispatch            4x: %.1f ns (%.1f ns/perm)\n", ns, ns/4);
        }
#if defined(__x86_64__) || defined(__i386__)
        {
            dap_keccak_x4_state_t st = {0}; dap_keccak_x4_init(&st);
            for (int i = 0; i < 1000; i++) dap_keccak_x4_permute_avx2(&st);
            uint64_t t0 = s_rdtsc_or_clock();
            for (int i = 0; i < N; i++) dap_keccak_x4_permute_avx2(&st);
            uint64_t t1 = s_rdtsc_or_clock();
            double ns = (double)(t1-t0)/N;
            printf("  x4 direct avx2         4x: %.1f ns (%.1f ns/perm)\n", ns, ns/4);
        }
        {
            dap_keccak_x4_state_t st = {0}; dap_keccak_x4_init(&st);
            for (int i = 0; i < 1000; i++) dap_keccak_x4_permute_avx512(&st);
            uint64_t t0 = s_rdtsc_or_clock();
            for (int i = 0; i < N; i++) dap_keccak_x4_permute_avx512(&st);
            uint64_t t1 = s_rdtsc_or_clock();
            double ns = (double)(t1-t0)/N;
            printf("  x4 direct avx512       4x: %.1f ns (%.1f ns/perm)\n", ns, ns/4);
        }
#endif
        printf("\n=== Done (Keccak only) ===\n");
        dap_common_deinit();
        return 0;
    }

    fflush(stdout);
    s_benchmark_mlkem();
    if (!getenv("BENCH_MLKEM_ONLY")) {
        s_benchmark_mldsa();
        s_benchmark_chacha20();
        s_benchmark_aes();
        s_benchmark_ntru_prime();
    }

    printf("\n=== Done ===\n");
    dap_common_deinit();
    return 0;
}
